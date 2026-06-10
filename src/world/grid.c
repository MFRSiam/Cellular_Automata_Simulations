//
// grid.c - falling-sand simulation, procedural terrain, and streaming.
//
#include "grid.h"
#include "noise.h"
#include "biome.h"
#include "structure.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Installed by sim.c when the worker pool exists (see grid.h).
void (*GridParallelFor)(int count, void (*fn)(int index, void *ud), void *ud) = NULL;

// ---------------------------------------------------------------------------
// Thread-local RNG. GridUpdateStrip runs on several threads at once; raylib's
// global LCG would be a data race. xorshift32, lazily seeded per thread.
// Within this file every GetRandomValue call is redirected here.
// ---------------------------------------------------------------------------
#ifdef _MSC_VER
#define SIM_THREAD_LOCAL __declspec(thread)
#else
#define SIM_THREAD_LOCAL _Thread_local
#endif
static SIM_THREAD_LOCAL unsigned s_rngState = 0u;
static int SimRand(int min, int max) {
    unsigned x = s_rngState;
    if (x == 0u) x = 0x9E3779B9u ^ (unsigned)(uintptr_t)&s_rngState; // per-thread seed
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_rngState = x;
    return min + (int)(x % (unsigned)(max - min + 1));
}
#define GetRandomValue SimRand

static inline CellType TypeOf(Cell c)   { return MATERIALS[c].type; }
static inline int      DensityOf(Cell c){ return MATERIALS[c].density; }
static inline bool     Flammable(Cell c){ return MATERIALS[c].flammable; }
static inline int      Idx(const Grid *g, int x, int y) { return y * g->width + x; }

// ---------------------------------------------------------------------------
// Persistence store: an open-addressed hash map keyed by absolute world cell.
// Cells leaving the simulation window are saved here and restored when they
// scroll back in, so player edits persist across the infinite world.
// (A new game / regenerate clears it.)
// ---------------------------------------------------------------------------
typedef struct StoreEntry { uint64_t key; Cell cell; uint8_t life; bool used; } StoreEntry;
static StoreEntry *s_store = NULL;
static size_t s_cap = 0, s_count = 0;

static uint64_t PackKey(int x, int y) { return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y; }
static uint64_t MixKey(uint64_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; return k; }

static void StoreClear(void) { if (s_store) memset(s_store, 0, s_cap * sizeof(StoreEntry)); s_count = 0; }
static void StoreFree(void)  { free(s_store); s_store = NULL; s_cap = s_count = 0; }

static void StoreGrow(void) {
    size_t ncap = s_cap ? s_cap * 2 : 8192; // power of two
    StoreEntry *ns = calloc(ncap, sizeof(StoreEntry));
    for (size_t i = 0; i < s_cap; i++) {
        if (!s_store[i].used) continue;
        size_t j = MixKey(s_store[i].key) & (ncap - 1);
        while (ns[j].used) j = (j + 1) & (ncap - 1);
        ns[j] = s_store[i];
    }
    free(s_store);
    s_store = ns;
    s_cap = ncap;
}

static void StoreSet(int x, int y, Cell c, uint8_t life) {
    if ((s_count + 1) * 10 >= s_cap * 7) StoreGrow(); // keep load factor < 0.7
    uint64_t key = PackKey(x, y);
    size_t j = MixKey(key) & (s_cap - 1);
    while (s_store[j].used) {
        if (s_store[j].key == key) { s_store[j].cell = c; s_store[j].life = life; return; }
        j = (j + 1) & (s_cap - 1);
    }
    s_store[j] = (StoreEntry){.key = key, .cell = c, .life = life, .used = true};
    s_count++;
}

static bool StoreGet(int x, int y, Cell *c, uint8_t *life) {
    if (!s_store) return false;
    uint64_t key = PackKey(x, y);
    size_t j = MixKey(key) & (s_cap - 1);
    while (s_store[j].used) {
        if (s_store[j].key == key) { *c = s_store[j].cell; *life = s_store[j].life; return true; }
        j = (j + 1) & (s_cap - 1);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
Grid GridCreate(int windowWidth, int windowHeight) {
    Grid g = { .width = windowWidth / CELL_SIZE, .height = windowHeight / CELL_SIZE };
    size_t n = (size_t)g.width * g.height;
    g.cells   = calloc(n, sizeof(Cell));
    g.flow    = calloc(n, sizeof(int8_t));
    g.life    = calloc(n, sizeof(uint8_t));
    g.updated = calloc(n, sizeof(uint8_t));
    g.sCells  = calloc(n, sizeof(Cell));
    g.sFlow   = calloc(n, sizeof(int8_t));
    g.sLife   = calloc(n, sizeof(uint8_t));
    g.rCells  = calloc(n, sizeof(Cell));
    g.rLife   = calloc(n, sizeof(uint8_t));
    g.tilesX  = (g.width  + SIM_TILE - 1) / SIM_TILE;
    g.tilesY  = (g.height + SIM_TILE - 1) / SIM_TILE;
    g.tileNow  = calloc((size_t)g.tilesX * g.tilesY, 1);
    g.tileNext = calloc((size_t)g.tilesX * g.tilesY, 1);
    memset(g.tileNext, 1, (size_t)g.tilesX * g.tilesY); // first step: everything awake
    g.cave = (CaveParams){ .scale = 0.045f, .threshold = 0.5f, .mudThreshold = 0.55f,
                           .octaves = 4, .seed = 1337, .biomes = true };
    return g;
}

void GridFree(Grid *g) {
    free(g->cells); free(g->flow); free(g->life); free(g->updated);
    free(g->sCells); free(g->sFlow); free(g->sLife);
    free(g->rCells); free(g->rLife);
    free(g->tileNow); free(g->tileNext);
    StoreFree();
    *g = (Grid){0};
}

// Wake the 3x3 tile neighbourhood around a changed cell so anything that could
// react to the change (falling sand above, water up to its dispersion range)
// gets processed next step.
static inline void Wake(Grid *g, int x, int y) {
    int tx = x / SIM_TILE, ty = y / SIM_TILE;
    for (int j = ty - 1; j <= ty + 1; j++) {
        if (j < 0 || j >= g->tilesY) continue;
        for (int i = tx - 1; i <= tx + 1; i++)
            if (i >= 0 && i < g->tilesX) g->tileNext[j * g->tilesX + i] = 1;
    }
}
static void WakeAll(Grid *g) {
    memset(g->tileNext, 1, (size_t)g->tilesX * g->tilesY);
}

void GridClear(Grid *g) {
    size_t n = (size_t)g->width * g->height;
    memset(g->cells, 0, n * sizeof(Cell));
    memset(g->flow,  0, n * sizeof(int8_t));
    memset(g->life,  0, n * sizeof(uint8_t));
    WakeAll(g);
}

bool GridInBounds(const Grid *g, int x, int y) {
    return x >= 0 && x < g->width && y >= 0 && y < g->height;
}
Cell GridGet(const Grid *g, int x, int y) { return g->cells[Idx(g, x, y)]; }

void GridSet(Grid *g, int x, int y, Cell mat) {
    int i = Idx(g, x, y);
    g->cells[i] = mat;
    g->flow[i]  = 0;
    g->life[i]  = MATERIALS[mat].life;
    Wake(g, x, y);
}

void GridPaint(Grid *g, int cx, int cy, int radius, Cell mat) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            if (!GridInBounds(g, x, y)) continue;
            if (mat != CELL_EMPTY && GridGet(g, x, y) != CELL_EMPTY &&
                TypeOf(mat) == TYPE_GAS) continue; // gases don't overwrite solids
            GridSet(g, x, y, mat);
        }
    }
}

// ---------------------------------------------------------------------------
// Movement primitives
// ---------------------------------------------------------------------------
static bool CanSink(const Grid *g, Cell mover, int x, int y) {
    if (!GridInBounds(g, x, y)) return false;
    Cell t = GridGet(g, x, y);
    if (t == CELL_EMPTY) return true;
    CellType tt = TypeOf(t);
    return (tt == TYPE_LIQUID || tt == TYPE_GAS) && DensityOf(mover) > DensityOf(t);
}
static bool CanRise(const Grid *g, Cell mover, int x, int y) {
    if (!GridInBounds(g, x, y)) return false;
    Cell t = GridGet(g, x, y);
    if (t == CELL_EMPTY) return true;
    CellType tt = TypeOf(t);
    return (tt == TYPE_LIQUID || tt == TYPE_GAS) && DensityOf(mover) < DensityOf(t);
}
static void Move(Grid *g, int x, int y, int nx, int ny) {
    int a = Idx(g, x, y), b = Idx(g, nx, ny);
    Cell    tc = g->cells[a]; g->cells[a] = g->cells[b]; g->cells[b] = tc;
    int8_t  tf = g->flow[a];  g->flow[a]  = g->flow[b];  g->flow[b]  = tf;
    uint8_t tl = g->life[a];  g->life[a]  = g->life[b];  g->life[b]  = tl;
    g->updated[a] = g->updated[b] = 1;
    Wake(g, x, y);
    Wake(g, nx, ny);
}

// ---------------------------------------------------------------------------
// Generic motion rules
// ---------------------------------------------------------------------------
static void StepPowder(Grid *g, int x, int y, Cell self) {
    if (CanSink(g, self, x, y + 1)) { Move(g, x, y, x, y + 1); return; }
    int first = GetRandomValue(0, 1) ? 1 : -1;
    for (int i = 0; i < 2; i++) {
        int dx = (i == 0) ? first : -first;
        if (CanSink(g, self, x + dx, y + 1)) { Move(g, x, y, x + dx, y + 1); return; }
    }
}

static void StepLiquid(Grid *g, int x, int y, Cell self) {
    int i = Idx(g, x, y);
    if (CanSink(g, self, x, y + 1)) { Move(g, x, y, x, y + 1); return; }

    int dir = g->flow[i] != 0 ? g->flow[i] : (GetRandomValue(0, 1) ? 1 : -1);
    for (int s = 0; s < 2; s++) {
        int dx = (s == 0) ? dir : -dir;
        if (CanSink(g, self, x + dx, y + 1)) { g->flow[i] = (int8_t)dx; Move(g, x, y, x + dx, y + 1); return; }
    }

    int maxL = 0, maxR = 0; bool cliffL = false, cliffR = false;
    for (int k = 1; k <= WATER_DISPERSION; k++) {
        if (!CanSink(g, self, x - k, y)) break;
        maxL = k; if (CanSink(g, self, x - k, y + 1)) { cliffL = true; break; }
    }
    for (int k = 1; k <= WATER_DISPERSION; k++) {
        if (!CanSink(g, self, x + k, y)) break;
        maxR = k; if (CanSink(g, self, x + k, y + 1)) { cliffR = true; break; }
    }
    if (maxL == 0 && maxR == 0) { g->flow[i] = 0; return; }

    int dx;
    if (cliffL && !cliffR)      dx = -maxL;
    else if (cliffR && !cliffL) dx =  maxR;
    else if (dir < 0 && maxL)   dx = -maxL;
    else if (dir > 0 && maxR)   dx =  maxR;
    else                        dx = maxL ? -maxL : maxR;

    g->flow[i] = (int8_t)(dx < 0 ? -1 : 1);
    Move(g, x, y, x + dx, y);
}

static void StepGas(Grid *g, int x, int y, Cell self) {
    if (CanRise(g, self, x, y - 1)) { Move(g, x, y, x, y - 1); return; }
    int first = GetRandomValue(0, 1) ? 1 : -1;
    for (int i = 0; i < 2; i++) {
        int dx = (i == 0) ? first : -first;
        if (CanRise(g, self, x + dx, y - 1)) { Move(g, x, y, x + dx, y - 1); return; }
    }
    for (int i = 0; i < 2; i++) {
        int dx = (i == 0) ? first : -first;
        if (GridInBounds(g, x + dx, y) && GridGet(g, x + dx, y) == CELL_EMPTY) { Move(g, x, y, x + dx, y); return; }
    }
}

// ---------------------------------------------------------------------------
// Ripples
// ---------------------------------------------------------------------------
static void SpawnRipple(Grid *g, int cx, int cy) {
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!g->ripples[i].active) {
            g->ripples[i] = (Ripple){
                .x = (g->originX + cx) * CELL_SIZE + CELL_SIZE / 2.0f,
                .y = (g->originY + cy) * CELL_SIZE + CELL_SIZE / 2.0f,
                .radius = 1.0f, .life = 1.0f, .active = true };
            return;
        }
    }
}
static void UpdateRipples(Grid *g) {
    float dt = GetFrameTime();
    for (int i = 0; i < MAX_RIPPLES; i++) {
        Ripple *r = &g->ripples[i];
        if (!r->active) continue;
        r->radius += dt * 55.0f;
        r->life   -= dt * 2.0f;
        if (r->life <= 0.0f) r->active = false;
    }
}

// ---------------------------------------------------------------------------
// Reactions (return true when they consume/replace the cell)
// ---------------------------------------------------------------------------
static const int NX[4] = {0, 0, -1, 1};
static const int NY[4] = {-1, 1, 0, 0};

static void Ignite(Grid *g, int x, int y) {
    GridSet(g, x, y, CELL_FIRE);
    g->updated[Idx(g, x, y)] = 1;
}

// Materials that carry electricity (a spark will arc along them).
static inline bool Conductor(Cell c) {
    return c == CELL_METAL || c == CELL_COPPER || c == CELL_GOLD ||
           c == CELL_WATER || c == CELL_MERCURY || c == CELL_ACID || c == CELL_CRYSTAL;
}

// A blast: fills nearby air with fire, ignites flammables, and blows apart soft
// matter. Hard materials (metal/rock/glass/obsidian/crystal/copper) resist.
// Gunpowder neighbours are left intact so the spreading fire chains them.
static void Explode(Grid *g, int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (!GridInBounds(g, x, y)) continue;
            Cell c = GridGet(g, x, y);
            if (c == CELL_GUNPOWDER) continue;            // let the fire chain it
            if (c == CELL_EMPTY) { if (GetRandomValue(0, 100) < 65) GridSet(g, x, y, CELL_FIRE); }
            else if (Flammable(c)) Ignite(g, x, y);
            else {
                CellType t = TypeOf(c);
                if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
                    c != CELL_ROCK && c != CELL_BASALT && c != CELL_SANDSTONE &&
                    GetRandomValue(0, 100) < 55)
                    GridSet(g, x, y, GetRandomValue(0, 2) ? CELL_SMOKE : CELL_EMPTY);
            }
        }
    }
    GridSet(g, cx, cy, CELL_FIRE);
}

static bool StepFire(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    int fuel = 0, water = 0, waterAx = -1, waterAy = -1;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) { water++; waterAx = ax; waterAy = ay; }
        else if (c == CELL_ICE) { if (GetRandomValue(0, 100) < 4) GridSet(g, ax, ay, CELL_WATER); }
        else if (Flammable(c)) {
            fuel++;
            // Oil & gas catch readily; solids (wood/moss) smoulder slowly.
            int chance = (c == CELL_OIL || c == CELL_GAS) ? 120 : 25;
            if (GetRandomValue(0, 1000) < chance) Ignite(g, ax, ay);
        }
    }

    // Fuelled fires throw short-lived flame tongues upward, so a burning
    // surface reads as a proper blaze instead of a thin glowing line.
    if (fuel > 0 && GetRandomValue(0, 100) < 35) {
        int lx = x + GetRandomValue(-1, 1);
        if (GridInBounds(g, lx, y - 1) && GridGet(g, lx, y - 1) == CELL_EMPTY) {
            GridSet(g, lx, y - 1, CELL_FIRE);
            g->life[Idx(g, lx, y - 1)] = (uint8_t)GetRandomValue(8, 28); // brief tongue
        }
    }

    // Water no longer flash-boils on contact. It only occasionally evaporates a
    // single touching cell, so a flame next to water makes very little vapour.
    if (water > 0 && GetRandomValue(0, 100) < 5)
        GridSet(g, waterAx, waterAy, CELL_VAPOR);

    if (fuel > 0) {
        // Being fed by fuel keeps the fire burning - it won't die out while
        // there's wood/oil/etc. next to it, and water can't easily drown it.
        if (g->life[i] < MATERIALS[CELL_FIRE].life) g->life[i] = MATERIALS[CELL_FIRE].life;
    } else {
        // No fuel: water can smother it; otherwise it burns down to smoke.
        if (water >= 2 && GetRandomValue(0, 100) < 35) {
            GridSet(g, x, y, GetRandomValue(0, 3) == 0 ? CELL_SMOKE : CELL_EMPTY);
            return true;
        }
        if (g->life[i] == 0) {
            int r = GetRandomValue(0, 9); // burnt out: soot/ash residue or clears
            GridSet(g, x, y, r < 2 ? CELL_ASH : (r < 6 ? CELL_SMOKE : CELL_EMPTY));
            return true;
        }
        g->life[i]--;
    }

    StepGas(g, x, y, CELL_FIRE);
    return true;
}

// Acid carries POTENCY in its life field (starts at MATERIALS[CELL_ACID].life).
// Corroding a cell consumes a big chunk of potency - acid is spent by the
// reaction, it doesn't eat forever. Touching water drains potency gradually
// (dilution); only when fully drained does the cell become plain water.
// The render tints acid paler as it weakens, so you can SEE it dying.
static bool StepAcid(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    int water = 0;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) { water++; continue; }
        CellType t = TypeOf(c);
        // Eats solids/powders, but acid-proof materials (glass, metal,
        // obsidian) contain it instead.
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
            GetRandomValue(0, 100) < 8) {
            GridSet(g, ax, ay, GetRandomValue(0, 100) < 20 ? CELL_ACID_GAS : CELL_EMPTY);
            const int cost = 60; // the reaction consumes the acid itself
            if (g->life[i] <= cost) {
                GridSet(g, x, y, GetRandomValue(0, 3) ? CELL_EMPTY : CELL_ACID_GAS); // spent
                return true;
            }
            g->life[i] -= cost;
        }
    }
    // Dilution: each adjacent water steadily saps potency; fully drained acid
    // becomes water instead of flipping instantly.
    if (water > 0) {
        int drain = water * 2;
        if (g->life[i] <= drain) { GridSet(g, x, y, CELL_WATER); return true; }
        g->life[i] = (uint8_t)(g->life[i] - drain);
    }
    return false;
}

static bool StepSnow(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_FIRE || c == CELL_LAVA || c == CELL_VAPOR) { GridSet(g, x, y, CELL_WATER); return true; }
    }
    if (GetRandomValue(0, 2) == 0) return true; // flutter
    return false;
}

static bool StepIce(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_FIRE || c == CELL_LAVA) {
            if (GetRandomValue(0, 100) < 20) { GridSet(g, x, y, CELL_WATER); return true; }
        }
    }
    return true; // otherwise static
}

static bool StepVapor(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    bool atTop = !CanRise(g, CELL_VAPOR, x, y - 1);
    if (g->life[i] == 0 || (atTop && GetRandomValue(0, 100) < 4)) {
        GridSet(g, x, y, CELL_WATER); SpawnRipple(g, x, y); return true;
    }
    g->life[i]--;
    return false;
}

static bool StepSmoke(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    if (g->life[i] == 0) { GridSet(g, x, y, CELL_EMPTY); return true; }
    g->life[i]--;
    return false;
}

static bool StepLava(Grid *g, int x, int y) {
    bool hasAir = false;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_EMPTY) hasAir = true;
        if (c == CELL_WATER || c == CELL_VAPOR) {        // quenched -> black stone
            GridSet(g, ax, ay, CELL_VAPOR);
            GridSet(g, x, y, CELL_BASALT);
            SpawnRipple(g, x, y);
            return true;
        }
        // Everything lava melts, melts over TIME - heat takes a while to soak in.
        if (c == CELL_ICE   && GetRandomValue(0, 1000) < 150) GridSet(g, ax, ay, CELL_WATER);
        if (c == CELL_SAND  && GetRandomValue(0, 1000) < 50)  GridSet(g, ax, ay, CELL_MOLTEN_GLASS);
        if (c == CELL_GLASS && GetRandomValue(0, 1000) < 5)   GridSet(g, ax, ay, CELL_MOLTEN_GLASS);
        if (c == CELL_WAX   && GetRandomValue(0, 1000) < 40)  GridSet(g, ax, ay, CELL_MOLTEN_WAX);
        // Rock erodes back into lava VERY slowly - a lava pocket gnaws at its
        // surroundings over minutes, it doesn't tunnel.
        if ((c == CELL_ROCK || c == CELL_MUD || c == CELL_SANDSTONE || c == CELL_BASALT) &&
            GetRandomValue(0, 4000) < 1) GridSet(g, ax, ay, CELL_LAVA);
        if (Flammable(c) && GetRandomValue(0, 1000) < 40) Ignite(g, ax, ay);
    }
    // Only cools to obsidian when its surface is exposed to air; lava sealed
    // inside its shell (e.g. a contained lava lake) stays molten.
    if (hasAir && GetRandomValue(0, 2000) < 6) { GridSet(g, x, y, CELL_OBSIDIAN); return true; }
    return false;
}

// Molten glass (sand melted by lava) flows briefly then sets into glass.
static bool StepMoltenGlass(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER || c == CELL_VAPOR) {        // chilled -> sets instantly
            GridSet(g, ax, ay, CELL_VAPOR);
            GridSet(g, x, y, CELL_GLASS);
            return true;
        }
    }
    if (GetRandomValue(0, 200) < 3) { GridSet(g, x, y, CELL_GLASS); return true; } // air-cool
    return false;
}

static bool StepFlammableGas(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (GridInBounds(g, ax, ay) && GridGet(g, ax, ay) == CELL_FIRE) { Ignite(g, x, y); return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Extra "science" reactions: phase changes, biology, chemistry.
// ---------------------------------------------------------------------------

// Water freezes when it's cold enough: surrounded by ice/snow and no heat
// source nearby (a crude latent-heat / nucleation model).
static bool StepWaterChem(Grid *g, int x, int y) {
    int cold = 0, heat = 0;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_ICE || c == CELL_SNOW) cold++;
        else if (c == CELL_FIRE || c == CELL_LAVA || c == CELL_MOLTEN_GLASS) heat++;
    }
    if (heat == 0 && cold >= 2 && GetRandomValue(0, 1000) < 15) { GridSet(g, x, y, CELL_ICE); return true; }
    return false;
}

// Moss is a plant: it photosynthesises and colonises damp soil. When water is
// nearby it slowly spreads onto an adjacent mud / sand / sandstone cell.
static void StepMoss(Grid *g, int x, int y) {
    bool water = false;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (GridInBounds(g, ax, ay) && GridGet(g, ax, ay) == CELL_WATER) { water = true; break; }
    }
    if (!water || GetRandomValue(0, 1000) >= 20) return; // tuned for sleeping tiles
    int start = GetRandomValue(0, 3);
    for (int k = 0; k < 4; k++) {
        int n = (start + k) & 3;
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_MUD || c == CELL_SAND || c == CELL_SANDSTONE) { GridSet(g, ax, ay, CELL_MOSS); return; }
    }
}

// Wet clay fired by heat turns to stone (ceramics): mud next to fire or lava
// slowly bakes into sandstone.
static void StepMud(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if ((c == CELL_FIRE || c == CELL_LAVA) && GetRandomValue(0, 1000) < 3) {
            GridSet(g, x, y, CELL_SANDSTONE);
            return;
        }
    }
}

// Acidic gas: a corrosive vapour. Eats adjacent non-acid-proof matter (like
// acid, but airborne) - and like acid, the reaction CONSUMES the gas. It also
// slowly dissipates on its own. Caller still runs StepGas.
static bool StepAcidGas(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        CellType t = TypeOf(c);
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
            GetRandomValue(0, 100) < 3) {
            GridSet(g, ax, ay, CELL_EMPTY);
            if (GetRandomValue(0, 100) < 40) { GridSet(g, x, y, CELL_EMPTY); return true; } // spent
        }
    }
    if (GetRandomValue(0, 1000) < 2) { GridSet(g, x, y, CELL_EMPTY); return true; } // dissipate
    return false;
}

// Grass lives over mud: it climbs a little, and creeps sideways across mud
// surfaces (so it carpets the ground, including fresh mud left by worms).
#define GRASS_MAX 5
static void StepGrass(Grid *g, int x, int y) {
    // Probability tuned for sleeping tiles: settled areas are only evaluated on
    // the ambient wake (~4% of steps), so the per-evaluation chance is higher.
    if (GetRandomValue(0, 1000) >= 30) return;

    // Must be rooted: walk down through any grass stem to mud.
    int stem = 0; bool soil = false;
    for (int d = 1; d <= GRASS_MAX; d++) {
        if (!GridInBounds(g, x, y + d)) break;
        Cell c = GridGet(g, x, y + d);
        if (c == CELL_GRASS) { stem++; continue; }
        if (c == CELL_MUD)   soil = true;
        break;
    }
    if (!soil) return;

    // 1) climb upward a bit
    if (stem < GRASS_MAX && GridInBounds(g, x, y - 1) && GridGet(g, x, y - 1) == CELL_EMPTY &&
        GetRandomValue(0, 1) == 0) {
        GridSet(g, x, y - 1, CELL_GRASS);
        return;
    }
    // 2) propagate sideways onto an empty cell that also sits on mud/grass
    int dir = GetRandomValue(0, 1) ? 1 : -1;
    for (int s = 0; s < 2; s++) {
        int nx = x + (s == 0 ? dir : -dir);
        if (!GridInBounds(g, nx, y) || GridGet(g, nx, y) != CELL_EMPTY) continue;
        if (!GridInBounds(g, nx, y + 1)) continue;
        Cell below = GridGet(g, nx, y + 1);
        if (below == CELL_MUD || below == CELL_GRASS) { GridSet(g, nx, y, CELL_GRASS); return; }
    }
}

// Vines hang and grow downward from a solid/mossy ceiling (jungle flavour).
#define VINE_MAX 12
static void StepVine(Grid *g, int x, int y) {
    if (GetRandomValue(0, 1000) >= 20) return; // tuned for sleeping tiles
    int stem = 0; bool anchor = false;
    for (int d = 1; d <= VINE_MAX; d++) {
        if (!GridInBounds(g, x, y - d)) break;
        Cell c = GridGet(g, x, y - d);
        if (c == CELL_VINE) { stem++; continue; }
        if (c == CELL_MUD || c == CELL_MOSS || c == CELL_ROCK) anchor = true;
        break;
    }
    if (!anchor || stem >= VINE_MAX) return;
    if (GridInBounds(g, x, y + 1) && GridGet(g, x, y + 1) == CELL_EMPTY)
        GridSet(g, x, y + 1, CELL_VINE);
}

// Salt dissolves in water and melts ice/snow (depresses the freezing point).
static bool StepSalt(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if ((c == CELL_ICE || c == CELL_SNOW) && GetRandomValue(0, 100) < 30) {
            GridSet(g, ax, ay, CELL_WATER); GridSet(g, x, y, CELL_EMPTY); return true;
        }
        if (c == CELL_WATER && GetRandomValue(0, 100) < 6) { GridSet(g, x, y, CELL_EMPTY); return true; }
    }
    return false;
}

// Ash is the residue of fire; stirred into water it turns to mud (sludge/lye).
static bool StepAsh(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (GridInBounds(g, ax, ay) && GridGet(g, ax, ay) == CELL_WATER &&
            GetRandomValue(0, 100) < 5) { GridSet(g, x, y, CELL_MUD); return true; }
    }
    return false;
}

// Gunpowder detonates the instant it touches fire, lava or a spark.
static bool StepGunpowder(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_FIRE || c == CELL_LAVA || c == CELL_SPARK) { Explode(g, x, y, 3); return true; }
    }
    return false;
}

// Spark = electricity. It ignites flammables, detonates gunpowder, flash-boils
// water, and arcs along conductors (it moves into an empty cell next to one),
// fading after a few cells.
static bool StepSpark(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    bool nearCond = false;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (Conductor(c)) nearCond = true;
        if (c == CELL_GUNPOWDER) Explode(g, ax, ay, 3);
        else if (c == CELL_WATER) { if (GetRandomValue(0, 100) < 2) GridSet(g, ax, ay, CELL_VAPOR); }
        else if (Flammable(c) && GetRandomValue(0, 1000) < 400) Ignite(g, ax, ay);
    }
    if (g->life[i] == 0) { GridSet(g, x, y, CELL_EMPTY); return true; }
    g->life[i]--;

    // Arc along a conductor: hop into an empty neighbour (life carries over).
    if (nearCond && GetRandomValue(0, 100) < 70) {
        int start = GetRandomValue(0, 3);
        for (int k = 0; k < 4; k++) {
            int n = (start + k) & 3, ax = x + NX[n], ay = y + NY[n];
            if (GridInBounds(g, ax, ay) && GridGet(g, ax, ay) == CELL_EMPTY) {
                Move(g, x, y, ax, ay); return true;
            }
        }
    }
    StepGas(g, x, y, CELL_SPARK); // otherwise drift upward
    return true;
}

// Mercury is a heavy liquid metal: it slowly dissolves gold into amalgam, and
// near intense heat it boils off into toxic (acidic) fumes.
static bool StepMercury(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_GOLD && GetRandomValue(0, 100) < 3) { GridSet(g, ax, ay, CELL_MERCURY); return false; }
        if ((c == CELL_FIRE || c == CELL_LAVA) && GetRandomValue(0, 1000) < 8) {
            GridSet(g, x, y, CELL_ACID_GAS); return true; // toxic mercury vapour
        }
    }
    return false;
}

// Wax melts to molten wax when it touches a heat source.
static bool StepWax(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if ((c == CELL_FIRE || c == CELL_LAVA || c == CELL_MOLTEN_GLASS) &&
            GetRandomValue(0, 100) < 20) { GridSet(g, x, y, CELL_MOLTEN_WAX); return true; }
    }
    return false;
}

// Blood pools, dries up over time, and boils to smoke near intense heat.
static bool StepBlood(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if ((c == CELL_FIRE || c == CELL_LAVA) && GetRandomValue(0, 100) < 20) {
            GridSet(g, x, y, CELL_SMOKE); return true;
        }
    }
    if (GetRandomValue(0, 1000) < 2) { GridSet(g, x, y, CELL_EMPTY); return true; } // dries
    return false;
}

// Living reef: coral surrounded by water slowly encrusts adjacent sand.
static void StepCoral(Grid *g, int x, int y) {
    bool water = false; int sx = -1, sy = -1;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) water = true;
        else if (c == CELL_SAND) { sx = ax; sy = ay; }
    }
    if (water && sx >= 0 && GetRandomValue(0, 2000) < 5) GridSet(g, sx, sy, CELL_CORAL);
}

// Molten wax flows, then sets back to solid wax once it cools (or hits water).
static bool StepMoltenWax(Grid *g, int x, int y) {
    bool heat = false;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_FIRE || c == CELL_LAVA) heat = true;
        if (c == CELL_WATER) { GridSet(g, x, y, CELL_WAX); return true; } // chilled -> sets
    }
    if (!heat && GetRandomValue(0, 100) < 4) { GridSet(g, x, y, CELL_WAX); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Frame step
// ---------------------------------------------------------------------------
// One simulation step for the cell at (x,y).
static void StepCell(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    if (g->updated[i]) return;
    Cell c = g->cells[i];
    switch (c) {
                case CELL_SAND:  StepPowder(g, x, y, c); break;
                case CELL_WATER: if (!StepWaterChem(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_OIL:   StepLiquid(g, x, y, c); break;
                case CELL_ACID:  if (!StepAcid(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_SNOW:  if (!StepSnow(g, x, y)) StepPowder(g, x, y, c); break;
                case CELL_FIRE:  StepFire(g, x, y); break;
                case CELL_VAPOR: if (!StepVapor(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_SMOKE: if (!StepSmoke(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_GAS:   if (!StepFlammableGas(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_INERT_GAS: StepGas(g, x, y, c); break;
                case CELL_ACID_GAS:  if (!StepAcidGas(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_LAVA:  if (!StepLava(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_MOLTEN_GLASS: if (!StepMoltenGlass(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_ICE:   StepIce(g, x, y); break;
                case CELL_MOSS:  StepMoss(g, x, y); break;
                case CELL_MUD:   StepMud(g, x, y); break;
                case CELL_GRASS: StepGrass(g, x, y); break;
                case CELL_VINE:  StepVine(g, x, y); break;
                case CELL_GOLD:  StepPowder(g, x, y, c); break; // gold falls under gravity
                case CELL_SALT:  if (!StepSalt(g, x, y)) StepPowder(g, x, y, c); break;
                case CELL_ASH:   if (!StepAsh(g, x, y)) StepPowder(g, x, y, c); break;
                case CELL_GUNPOWDER: if (!StepGunpowder(g, x, y)) StepPowder(g, x, y, c); break;
                case CELL_SPARK: StepSpark(g, x, y); break;
                case CELL_MERCURY: if (!StepMercury(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_WAX:   StepWax(g, x, y); break;
                case CELL_MOLTEN_WAX: if (!StepMoltenWax(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_BLOOD: if (!StepBlood(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_CORAL: StepCoral(g, x, y); break;
                default: break; // rock / coal / glass / metal / crystal / copper are static
    }
}

void GridUpdatePrepare(Grid *g) {
    g->frame++;
    memset(g->updated, 0, (size_t)g->width * g->height);

    // Rotate tile activity: tiles woken last step are processed now; changes
    // made during this step wake tiles for the next one.
    size_t nTiles = (size_t)g->tilesX * g->tilesY;
    memcpy(g->tileNow, g->tileNext, nTiles);
    memset(g->tileNext, 0, nTiles);

    // Ambient tick: wake a few random tiles so slow background processes
    // (grass/moss/coral growth, lava cooling) continue in settled areas.
    int ambient = (int)(nTiles / 25) + 1; // ~4% of tiles per step
    for (int k = 0; k < ambient; k++)
        g->tileNow[GetRandomValue(0, (int)nTiles - 1)] = 1;

    int awake = 0;
    for (size_t k = 0; k < nTiles; k++) awake += g->tileNow[k];
    g->activeTiles = awake;
}

// Process the column range [x0,x1), bottom-up, honouring tile activity and the
// per-frame scan direction. Safe to call concurrently for ranges separated by
// an unprocessed strip wider than the maximum interaction reach (~7 cells).
void GridUpdateStrip(Grid *g, int x0, int x1) {
    bool ltr = (g->frame % 2 == 0);
    int t0 = x0 / SIM_TILE, t1 = (x1 - 1) / SIM_TILE;

    for (int y = g->height - 1; y >= 0; y--) {
        const uint8_t *rowTiles = &g->tileNow[(y / SIM_TILE) * g->tilesX];
        if (ltr) {
            for (int tx = t0; tx <= t1; tx++) {
                if (!rowTiles[tx]) continue;
                int a = tx * SIM_TILE;       if (a < x0) a = x0;
                int b = (tx + 1) * SIM_TILE; if (b > x1) b = x1;
                for (int x = a; x < b; x++) StepCell(g, x, y);
            }
        } else {
            for (int tx = t1; tx >= t0; tx--) {
                if (!rowTiles[tx]) continue;
                int a = tx * SIM_TILE;       if (a < x0) a = x0;
                int b = (tx + 1) * SIM_TILE; if (b > x1) b = x1;
                for (int x = b - 1; x >= a; x--) StepCell(g, x, y);
            }
        }
    }
}

void GridUpdateFinish(Grid *g) {
    UpdateRipples(g);
}

// Serial fallback (used by the editor's small canvas).
void GridUpdate(Grid *g) {
    GridUpdatePrepare(g);
    GridUpdateStrip(g, 0, g->width);
    GridUpdateFinish(g);
}

// ---------------------------------------------------------------------------
// Procedural terrain
// ---------------------------------------------------------------------------
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Per-cell deterministic pseudo-random in [0,1) (for rare gold etc.).
static float CellRandom(int wx, int wy, unsigned int seed) {
    unsigned int h = (unsigned)(wx * 374761393) ^ (unsigned)(wy * 668265263) ^ (seed * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / (float)0x1000000;
}

static int FloorDivI(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

// Openness field, tuned for a Noita-like silhouette: the BASE field is very
// smooth (low frequency, 2 octaves, no per-cell roughness at all) and ALL the
// character comes from a two-stage domain warp - a big sweeping distortion
// followed by a smaller swirl. That combination produces long flowing rock
// edges, overhangs and tongues instead of crumbly noise, while the boundary
// itself stays clean and smooth.
static bool IsOpen(const CaveParams *p, const BiomeInfo *bi, int wx, int wy) {
    float s   = p->scale * (bi ? bi->scaleMul : 1.0f);
    int   oct = ClampI(p->octaves + (bi ? bi->octaveDelta : 0), 2, 3); // keep it smooth
    float thr = p->threshold + (bi ? bi->opennessBias : 0.0f);

    // Frequency discipline (at the default scale 0.045):
    //   base wavelength ~140 cells - one cavern is a real place, not a pore
    //   warp amplitudes stay well under 1/3 of the base wavelength; pushing
    //   them past it folds the field over itself and SHREDS the terrain into
    //   slivers (the jagged-crack artifact).
    // Stage 1: huge gentle sweep - bends whole formations.
    float ax = wx + 36.0f * NoisePerlin2(wx * s * 0.06f + 12.3f, wy * s * 0.06f + 4.1f);
    float ay = wy + 36.0f * NoisePerlin2(wx * s * 0.06f + 88.7f, wy * s * 0.06f + 51.9f);
    // Stage 2: small swirl at the warped point - curls the edges.
    float bx = ax + 10.0f * NoisePerlin2(ax * s * 0.32f + 7.7f,  ay * s * 0.32f + 21.4f);
    float by = ay + 10.0f * NoisePerlin2(ax * s * 0.32f + 63.1f, ay * s * 0.32f + 5.8f);

    // Smooth anisotropic base: caverns wider than tall (walkable floors).
    float field = NoiseFbm(bx * s * 0.16f, by * s * 0.28f, oct);

    float depth = (float)wy * 0.00040f;                       // strata: airy top,
    field += depth < -0.08f ? -0.08f : (depth > 0.14f ? 0.14f : depth); // dense deep

    if (field < thr) return true;

    // ONE broad corridor system + very sparse narrow connectors. Corridors are
    // a feature you find, not a texture smeared over the rock.
    float r1 = NoiseFbm(wx * s * 0.22f + 41.3f, wy * s * 0.45f + 17.7f, 2);
    if (1.0f - fabsf(r1 * 2.0f - 1.0f) > 0.82f && field < thr + 0.20f) return true;
    float r2 = NoiseFbm(wx * s * 0.55f + 191.0f, wy * s * 0.80f + 67.0f, 2);
    return (1.0f - fabsf(r2 * 2.0f - 1.0f) > 0.94f) && field < thr + 0.10f;
}
// --- cached generation fields ------------------------------------------------
// GenRegion (below) samples biome + openness ONCE per cell into temp arrays
// with GEN_VMARGIN extra rows above/below, so all the vertical probes
// (stalactites, surface detection, plant anchors) are array reads instead of
// repeated noise evaluation. This is the difference between a regen costing
// ~40 noise calls per cell and ~12.
#define GEN_VMARGIN 8

// open[] is indexed [(ly + GEN_VMARGIN) * w + lx] for ly in [-MARGIN, h+MARGIN).
static inline bool COpen(const uint8_t *open, int w, int lx, int ly) {
    return open[(ly + GEN_VMARGIN) * w + lx] != 0;
}
static int CDistUp(const uint8_t *open, int w, int lx, int ly, int maxd) {
    for (int d = 1; d <= maxd; d++) if (!COpen(open, w, lx, ly - d)) return d;
    return 0;
}
static int CDistDown(const uint8_t *open, int w, int lx, int ly, int maxd) {
    for (int d = 1; d <= maxd; d++) if (!COpen(open, w, lx, ly + d)) return d;
    return 0;
}

// Stalactite (ceiling) / stalagmite (floor) test: deterministic per-column
// length so the rock spikes form contiguous cones.
static bool CSpike(unsigned int seed, const uint8_t *open, int w, int lx, int ly,
                   int wx, bool ceiling) {
    int d = ceiling ? CDistUp(open, w, lx, ly, GEN_VMARGIN) : CDistDown(open, w, lx, ly, GEN_VMARGIN);
    if (d == 0) return false;
    unsigned h = (unsigned)(wx * 374761393) ^ (ceiling ? 0x1111u : 0x2222u) ^ (seed * 9176u);
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    if ((h & 7u) >= 2u) return false;       // ~1 in 4 columns grows a spike
    int len = 2 + (int)((h >> 8) % 6u);     // 2..7 cells long - reads as a cone
    return d <= len;
}

// Contained liquid pockets, ANCHORED per region instead of stamped from a
// global band field. The world is divided into POOL_REG x POOL_REG regions;
// each region's hash decides whether it hosts one pocket, where, how big, and
// (by depth + biome) what's inside. Pockets are lens-shaped (wider than tall,
// like real ponds), with an organic wobbled edge. Interior and shell use the
// SAME wobbled distance, so the 3-cell shell is always closed - nothing leaks
// until the player digs in.
//   water -> rock/mud shell      oil  -> wood shell (torch it...)
//   acid  -> glass/metal shell   lava -> obsidian/copper shell
//   deep gold pockets -> rock shell (sealed treasure; gold pours out when cut)
#define POOL_REG 96
static bool PoolAt(const CaveParams *p, const BiomeInfo *bi, Biome b, int wx, int wy, Cell *out) {
    (void)bi;
    int rx = FloorDivI(wx, POOL_REG), ry = FloorDivI(wy, POOL_REG);
    unsigned h = ((unsigned)rx * 73856093u) ^ ((unsigned)ry * 19349663u) ^ (p->seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    if ((h % 100u) >= 30u) return false;          // ~30% of regions host a pocket

    int cx = rx * POOL_REG + POOL_REG / 2 + (int)((h >> 8)  % 17u) - 8;
    int cy = ry * POOL_REG + POOL_REG / 2 + (int)((h >> 12) % 17u) - 8;
    int radius = 9 + (int)((h >> 16) % 9u);       // 9..17 cells wide
    const int shell = 3;

    int dx = wx - cx, dy = wy - cy;
    int bound = radius + shell + 4;
    if (dx * dx + dy * dy > bound * bound) return false;    // cheap reject

    // Lens metric (y squashed) + organic wobble shared by interior AND shell.
    float d = sqrtf((float)(dx * dx) + (float)(dy * dy) * 2.2f)
            + NoisePerlin2(wx * 0.13f + 7.0f, wy * 0.13f + 3.0f) * 2.0f;
    if (d > radius + shell) return false;

    // Contents by biome and the POCKET's depth (cy), so the whole pocket agrees.
    Cell liq, sh1, sh2;
    unsigned pick = (h >> 20) & 7u;
    if (b == BIOME_CORAL)     { liq = CELL_WATER; sh1 = CELL_CORAL;    sh2 = CELL_CORAL; }
    else if (b == BIOME_COLD) { liq = CELL_WATER; sh1 = CELL_ICE;      sh2 = CELL_ICE;   }
    else if (cy > 280) {      // deep: lava lakes, sealed gold, oil
        if (pick < 4)         { liq = CELL_LAVA;  sh1 = CELL_OBSIDIAN; sh2 = CELL_COPPER; }
        else if (pick < 6)    { liq = CELL_GOLD;  sh1 = CELL_ROCK;     sh2 = CELL_ROCK;   }
        else                  { liq = CELL_OIL;   sh1 = CELL_WOOD;     sh2 = CELL_WOOD;   }
    } else if (cy > 80) {     // mid: water, oil, acid
        if (pick < 3)         { liq = CELL_WATER; sh1 = CELL_ROCK;     sh2 = CELL_MUD;    }
        else if (pick < 6)    { liq = CELL_OIL;   sh1 = CELL_WOOD;     sh2 = CELL_WOOD;   }
        else                  { liq = CELL_ACID;  sh1 = CELL_GLASS;    sh2 = CELL_METAL;  }
    } else {                  // surface band: ponds, the odd oil cache
        if (pick < 5)         { liq = CELL_WATER; sh1 = CELL_ROCK;     sh2 = CELL_MUD;    }
        else                  { liq = CELL_OIL;   sh1 = CELL_WOOD;     sh2 = CELL_WOOD;   }
    }

    if (d <= radius) *out = liq;
    else             *out = (CellRandom(wx, wy, p->seed ^ 0x9E37u) < 0.15f) ? sh2 : sh1;
    return true;
}

// Material for a solid (wall) cell. `open/w/lx/ly` give cached-field access for
// the surface probes. Layered like a real cross-section:
//   top-soil  - a biome cover on any wall just under open air (mud / sand /
//               snow-over-ice), so floors look dressed and plants can root
//   veins     - elongated ridged streaks of ore: coal shallow, copper mid,
//               gold deep (gold pours out when you cut a vein - it's a powder)
//   strata    - the deep world fades into basalt / obsidian
static Cell WallMaterial(const CaveParams *p, const BiomeInfo *bi, Biome b, int wx, int wy,
                         const uint8_t *open, int w, int lx, int ly) {
    if (b == BIOME_VOID) {
        // Out-of-this-world: dark obsidian veined with shiny copper and
        // studded with glowing crystals.
        if (CellRandom(wx, wy, p->seed ^ 0x51A3u) < 0.05f) return CELL_CRYSTAL;
        float vv = NoiseFbm(wx * p->scale * 1.6f + 200.0f, wy * p->scale * 1.6f + 90.0f, 2);
        if (vv > 0.62f) return CELL_COPPER;
        return CELL_OBSIDIAN;
    }

    // Top-soil: walls within 3 cells under open air get a biome cover layer.
    int soil = 0;
    for (int k = 1; k <= 3; k++)
        if (COpen(open, w, lx, ly - k)) { soil = k; break; }
    if (soil) {
        switch (b) {
            case BIOME_JUNGLE:
            case BIOME_OPEN:  return CELL_MUD;                            // soil
            case BIOME_COLD:  return soil == 1 ? CELL_SNOW : CELL_ICE;    // snowcap
            case BIOME_SANDY:
            case BIOME_CORAL: return CELL_SAND;                           // dunes
            default: break;                                               // barren
        }
    }
    if (b == BIOME_SANDY) return CELL_SANDSTONE; // solid under the loose sand

    // Jittered depth coordinate shared by veins and strata (slow undulation).
    float band = wy + NoiseFbm(wx * 0.008f + 31.0f, wy * 0.008f + 77.0f, 2) * 50.0f - 25.0f;

    // Ore veins: LOW frequency + tight threshold = a few thick, readable veins
    // you discover - not scraggle wallpapered over every wall.
    float vr = NoiseFbm(wx * p->scale * 0.45f + 641.0f, wy * p->scale * 0.9f + 13.0f, 2);
    if (1.0f - fabsf(vr * 2.0f - 1.0f) > 0.94f) {
        if (band > 380.0f) return CELL_GOLD;
        if (band > 160.0f) return CELL_COPPER;
        return CELL_COAL;
    }
    if (CellRandom(wx, wy, p->seed) < 0.0004f) return CELL_GOLD; // rare glints

    // Deep strata: blend toward basalt with obsidian flecks below ~band 260.
    if (band > 260.0f) {
        float m = (band - 260.0f) / 220.0f;
        if (m > 0.92f) m = 0.92f;
        if (CellRandom(wx, wy, p->seed ^ 0xBEEFu) < m)
            return (CellRandom(wx, wy, p->seed ^ 0x0B51u) < 0.06f) ? CELL_OBSIDIAN : CELL_BASALT;
    }

    // Secondary material as LARGE coherent slabs (low frequency), not speckle -
    // big readable patches are a huge part of the hand-painted look.
    float mud = NoiseFbm(wx * p->scale * 0.35f + 100.0f, wy * p->scale * 0.35f + 100.0f, 2);
    Cell wallA = bi ? bi->wallPrimary : CELL_ROCK;
    Cell wallB = bi ? bi->wallSecondary : CELL_MUD;
    return (mud > p->mudThreshold) ? wallB : wallA;
}

// Fill a buffer-rect with freshly generated terrain.
// Pass 1 samples biome (with border dither) + openness once per cell into temp
// arrays; pass 2 decides materials with all probes as array reads. Both passes
// are row-band parallel via GridParallelFor (generation is pure functions of
// world coordinates + seed, so banding cannot change the result).
#define GEN_BAND 16

typedef struct GenCtx {
    Grid *g;
    const CaveParams *p;
    int bx0, by0, rw, rh, wx0, wy0, H;
    uint8_t *open, *biome;
} GenCtx;

static void GenPass1Band(int band, void *ud) {
    GenCtx *c = ud;
    const CaveParams *p = c->p;
    int y0 = band * GEN_BAND, y1 = y0 + GEN_BAND;
    if (y1 > c->H) y1 = c->H;
    for (int ly = y0; ly < y1; ly++) {
        int wy = c->wy0 + ly - GEN_VMARGIN;
        for (int lx = 0; lx < c->rw; lx++) {
            int wx = c->wx0 + lx;
            Biome b = BIOME_ROCKY;
            if (p->biomes) {
                b = BiomeAt(wx, wy);
                // Border blending: near a boundary the two biomes interleave in
                // LARGE smooth patches (low-frequency mask) - reads like geology
                // mixing, not per-cell static.
                Biome bAlt = BiomeAt(wx + 5, wy + 3);
                if (b != bAlt && NoiseFbm(wx * 0.02f + 9.0f, wy * 0.02f + 4.0f, 2) < 0.5f) b = bAlt;
            }
            const BiomeInfo *bi = p->biomes ? &BIOMES[b] : NULL;
            c->biome[ly * c->rw + lx] = (uint8_t)b;
            c->open [ly * c->rw + lx] = IsOpen(p, bi, wx, wy) ? 1 : 0;
        }
    }
}

static void GenPass2Band(int band, void *ud) {
    GenCtx *c = ud;
    Grid *g = c->g;
    const CaveParams *p = c->p;
    int rw = c->rw, M = GEN_VMARGIN;
    int y0 = band * GEN_BAND, y1 = y0 + GEN_BAND;
    if (y1 > c->rh) y1 = c->rh;
    for (int ly = y0; ly < y1; ly++) {
        int wy = c->wy0 + ly;
        for (int lx = 0; lx < rw; lx++) {
            int wx = c->wx0 + lx;
            Cell cell = CELL_EMPTY;

            // Hand-made structures take priority wherever they are placed.
            Cell s = StructureSampleAt(p->seed, wx, wy);
            if (s != CELL_EMPTY) { GridSet(g, c->bx0 + lx, c->by0 + ly, s); continue; }

            Biome b = (Biome)c->biome[(ly + M) * rw + lx];
            const BiomeInfo *bi = p->biomes ? &BIOMES[b] : NULL;

            // Contained pools override the cave so their shells always seal.
            if (!PoolAt(p, bi, b, wx, wy, &cell)) {
                if (COpen(c->open, rw, lx, ly)) {
                    if (CSpike(p->seed, c->open, rw, lx, ly, wx, true) ||
                        CSpike(p->seed, c->open, rw, lx, ly, wx, false)) {
                        // Stalactites / stalagmites in the biome's own voice:
                        // icicles, coral fingers, obsidian fangs...
                        switch (b) {
                            case BIOME_COLD:  cell = CELL_ICE;       break;
                            case BIOME_SANDY: cell = CELL_SANDSTONE; break;
                            case BIOME_CORAL: cell = CELL_CORAL;     break;
                            case BIOME_VOID:  cell = CELL_OBSIDIAN;  break;
                            default:          cell = CELL_ROCK;      break;
                        }
                    } else if ((b == BIOME_JUNGLE || b == BIOME_OPEN) &&
                               !COpen(c->open, rw, lx, ly + 1) &&
                               WallMaterial(p, bi, b, wx, wy + 1, c->open, rw, lx, ly + 1) == CELL_MUD &&
                               CellRandom(wx, wy, p->seed ^ 0x6Eu) < 0.85f) {
                        cell = CELL_GRASS;           // grass carpets mud floors (near-solid)
                    } else if (b == BIOME_JUNGLE && !COpen(c->open, rw, lx, ly - 1) &&
                               CellRandom(wx, wy, p->seed ^ 0x71u) < 0.30f) {
                        cell = CELL_VINE;            // vines drape jungle ceilings
                    }
                } else {
                    cell = WallMaterial(p, bi, b, wx, wy, c->open, rw, lx, ly);
                }
            }
            GridSet(g, c->bx0 + lx, c->by0 + ly, cell);
        }
    }
}

static void RunBands(int rows, void (*fn)(int, void *), GenCtx *ctx) {
    int bands = (rows + GEN_BAND - 1) / GEN_BAND;
    if (GridParallelFor && bands > 1) GridParallelFor(bands, fn, ctx);
    else for (int k = 0; k < bands; k++) fn(k, ctx);
}

static void GenRegion(Grid *g, int bx0, int by0, int rw, int rh) {
    GenCtx ctx = {
        .g = g, .p = &g->cave,
        .bx0 = bx0, .by0 = by0, .rw = rw, .rh = rh,
        .wx0 = g->originX + bx0, .wy0 = g->originY + by0,
        .H = rh + 2 * GEN_VMARGIN,
    };
    ctx.open  = malloc((size_t)rw * ctx.H);
    ctx.biome = malloc((size_t)rw * ctx.H);
    if (!ctx.open || !ctx.biome) { free(ctx.open); free(ctx.biome); return; }

    RunBands(ctx.H, GenPass1Band, &ctx); // pass 1 fully completes first
    RunBands(rh,    GenPass2Band, &ctx);

    free(ctx.open);
    free(ctx.biome);
}

void GridRegenerate(Grid *g) {
    StoreClear(); // a fresh layout: forget previous edits
    NoiseInit(g->cave.seed);
    GenRegion(g, 0, 0, g->width, g->height);
    for (int i = 0; i < MAX_RIPPLES; i++) g->ripples[i].active = false;
    WakeAll(g);
}

// Save a buffer-rect into the persistence store (uses the CURRENT origin).
static void SaveRect(Grid *g, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int i = y * g->width + x;
            StoreSet(g->originX + x, g->originY + y, g->cells[i], g->life[i]);
        }
}

// Generate an exposed buffer-rect, then overlay any persisted edits on top.
static void FillRect(Grid *g, int x0, int y0, int x1, int y1) {
    if (x0 >= x1 || y0 >= y1) return;
    GenRegion(g, x0, y0, x1 - x0, y1 - y0);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            Cell c; uint8_t lf;
            if (StoreGet(g->originX + x, g->originY + y, &c, &lf)) {
                GridSet(g, x, y, c);
                g->life[y * g->width + x] = lf;
            }
        }
}

void GridStreamTo(Grid *g, int nox, int noy) {
    // Quantise the origin to 8-cell steps so exposed strips arrive in batches
    // (amortising GenRegion's vertical margin) instead of 1-cell slivers every
    // frame. The buffer's spare edge absorbs the rounding; the camera doesn't
    // care where the window origin sits.
    nox = FloorDivI(nox, 8) * 8;
    noy = FloorDivI(noy, 8) * 8;
    int dx = nox - g->originX, dy = noy - g->originY;
    if (dx == 0 && dy == 0) return;
    int w = g->width, h = g->height;

    // 1) Save ONLY the departing strips (cells with no home in the new window).
    int sx0 = dx > 0 ? (dx < w ? dx : w) : 0;          // surviving column range
    int sx1 = dx < 0 ? (w + dx > 0 ? w + dx : 0) : w;
    if (sx0 > 0) SaveRect(g, 0, 0, sx0, h);
    if (sx1 < w) SaveRect(g, sx1, 0, w, h);
    if (sx0 < sx1) {
        if (dy > 0) SaveRect(g, sx0, 0, sx1, dy < h ? dy : h);
        if (dy < 0) SaveRect(g, sx0, h + dy > 0 ? h + dy : 0, sx1, h);
    }

    // 2) Shift surviving cells into place via the scratch buffers.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int di = y * w + x, sx = x + dx, sy = y + dy;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                int si = sy * w + sx;
                g->sCells[di] = g->cells[si]; g->sFlow[di] = g->flow[si]; g->sLife[di] = g->life[si];
            } else {
                g->sCells[di] = CELL_EMPTY; g->sFlow[di] = 0; g->sLife[di] = 0;
            }
        }
    }
    size_t n = (size_t)w * h;
    memcpy(g->cells, g->sCells, n * sizeof(Cell));
    memcpy(g->flow,  g->sFlow,  n * sizeof(int8_t));
    memcpy(g->life,  g->sLife,  n * sizeof(uint8_t));
    g->originX = nox; g->originY = noy;

    // 3) Generate the exposed strips (persisted edits overlaid by FillRect).
    int ex0 = dx < 0 ? (-dx < w ? -dx : w) : 0;        // exposed left strip width
    int ex1 = dx > 0 ? (w - dx > 0 ? w - dx : 0) : w;  // first exposed right column
    if (ex0 > 0) FillRect(g, 0, 0, ex0, h);
    if (ex1 < w) FillRect(g, ex1, 0, w, h);
    if (ex0 < ex1) {
        if (dy < 0) FillRect(g, ex0, 0, ex1, -dy < h ? -dy : h);
        if (dy > 0) FillRect(g, ex0, h - dy > 0 ? h - dy : 0, ex1, h);
    }

    // The world shifted under the tile flags - wake everything once so moved
    // liquids/gases re-settle, then the quiet tiles fall back asleep.
    WakeAll(g);
}

// ---------------------------------------------------------------------------
// Rendering of cells (post effects are applied later by the renderer)
// ---------------------------------------------------------------------------
static unsigned char ClampB(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }
static int Hash(int x, int y) {
    int h = x * 374761393 + y * 668265263;
    h = (h ^ (h >> 13)) * 1274126177;
    return (h ^ (h >> 16)) & 0xFF;
}

// Scale a colour so its brightest channel is at most `maxv`, preserving hue.
// Keeps non-emissive materials below the bloom threshold so they don't glow.
static Color CapBrightness(Color c, int maxv) {
    int m = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    if (m <= maxv || m == 0) return c;
    float k = maxv / (float)m;
    c.r = (unsigned char)(c.r * k); c.g = (unsigned char)(c.g * k); c.b = (unsigned char)(c.b * k);
    return c;
}

// Take a consistent copy of the simulation state for the renderer. Called by
// the main thread under the sim lock; the actual drawing then runs unlocked so
// the worker can advance the next step in parallel.
void GridSnapshot(Grid *g) {
    size_t n = (size_t)g->width * g->height;
    memcpy(g->rCells, g->cells, n * sizeof(Cell));
    memcpy(g->rLife,  g->life,  n * sizeof(uint8_t));
    memcpy(g->rRipples, g->ripples, sizeof(g->ripples));
    g->rOriginX = g->originX;
    g->rOriginY = g->originY;
}

// Colour of the snapshot cell at buffer (x,y), index i. Animated effects keyed
// on WORLD coordinates so speckle/shimmer stick to the terrain instead of
// swimming across it as the window streams.
static Color CellColor(const Grid *g, int x, int y, int i, float t) {
    Cell mat = g->rCells[i];
    Color c = MATERIALS[mat].color;
    int wx = g->rOriginX + x, wy = g->rOriginY + y;
    int n = Hash(wx, wy) - 128;

    switch (mat) {
        case CELL_WATER: {
            int s = (int)(sinf(wx * 0.4f + wy * 0.2f + t * 3.0f) * 18.0f);
            c.g = ClampB(c.g + s); c.b = ClampB(c.b + s);
            if (y == 0 || g->rCells[i - g->width] != CELL_WATER) { // surface foam
                c.r = ClampB(c.r + 70); c.g = ClampB(c.g + 60); c.b = ClampB(c.b + 25);
            }
        } break;
        case CELL_FIRE: {
            float k = g->rLife[i] / (float)MATERIALS[CELL_FIRE].life;
            int f = GetRandomValue(-30, 30);
            // White-hot core when fresh, cooling to deep orange as it dies.
            float core = k > 0.7f ? (k - 0.7f) / 0.3f : 0.0f;
            c.r = 255;
            c.g = ClampB((int)(110 + 130 * k) + f);
            c.b = ClampB((int)(25 * k + 170 * core) + f / 3);
        } break;
        case CELL_ACID: {
            // Tint by potency: vivid green when strong, pale watery teal when
            // nearly diluted - you can see acid weakening.
            float k = g->rLife[i] / (float)MATERIALS[CELL_ACID].life;
            c.r = ClampB((int)(120 * k +  70 * (1.0f - k)));
            c.g = ClampB((int)(205 * k + 165 * (1.0f - k)));
            c.b = ClampB((int)( 60 * k + 205 * (1.0f - k)));
        } break;
        case CELL_LAVA: {
            float glow = sinf(wx * 0.5f + wy * 0.5f + t * 4.0f) * 0.5f + 0.5f;
            c.r = ClampB(220 + (int)(35 * glow));
            c.g = ClampB(60 + (int)(90 * glow) + n / 6);
            c.b = ClampB(10 + (int)(20 * glow));
        } break;
        case CELL_GOLD: {
            float pulse = sinf(t * 2.5f + (wx + wy) * 0.6f) * 0.5f + 0.5f;
            c.r = ClampB(c.r + (int)(20 * pulse));
            c.g = ClampB(c.g + (int)(30 * pulse));
        } break;
        case CELL_MOLTEN_GLASS: {
            float glow = sinf(wx * 0.6f + wy * 0.4f + t * 5.0f) * 0.5f + 0.5f;
            c.r = 255;
            c.g = ClampB(150 + (int)(80 * glow));
            c.b = ClampB(40 + (int)(40 * glow));
        } break;
        case CELL_CRYSTAL: {
            float pulse = sinf(t * 1.8f + (wx * 0.7f - wy * 0.5f)) * 0.5f + 0.5f;
            c.r = ClampB(120 + (int)(60 * pulse));
            c.g = ClampB(190 + (int)(50 * pulse));
            c.b = 255;
        } break;
        case CELL_COPPER: {
            float spec = sinf(wx * 0.9f + wy * 0.5f + t * 1.5f);
            int s = (int)(fmaxf(spec, 0.0f) * 55.0f);
            c.r = ClampB(c.r + s); c.g = ClampB(c.g + s * 3 / 4); c.b = ClampB(c.b + s / 2);
        } break;
        case CELL_MERCURY: {
            float spec = sinf(wx * 0.8f + wy * 0.4f + t * 2.0f);
            int s = (int)(fmaxf(spec, 0.0f) * 60.0f);
            c.r = ClampB(c.r + s); c.g = ClampB(c.g + s); c.b = ClampB(c.b + s);
        } break;
        case CELL_SPARK: {
            int f = GetRandomValue(-45, 45);
            c.r = ClampB(190 + f); c.g = ClampB(225 + f); c.b = 255;
        } break;
        case CELL_MOLTEN_WAX: {
            float glow = sinf(wx * 0.5f + wy * 0.3f + t * 3.0f) * 0.5f + 0.5f;
            c.r = ClampB(c.r + (int)(25 * glow));
            c.g = ClampB(c.g + (int)(18 * glow));
        } break;
        case CELL_CORAL: {
            switch (Hash(wx * 3, wy * 7) & 3) { // vivid multi-hue reef
                case 0: c = (Color){255, 110, 150, 255}; break; // pink
                case 1: c = (Color){255, 150,  80, 255}; break; // orange
                case 2: c = (Color){180, 110, 220, 255}; break; // purple
                default:c = (Color){ 90, 210, 200, 255}; break; // teal
            }
            c.r = ClampB(c.r + n / 14); c.g = ClampB(c.g + n / 14); c.b = ClampB(c.b + n / 14);
        } break;
        case CELL_SMOKE:
        case CELL_VAPOR: {
            float k = g->rLife[i] / (float)MATERIALS[mat].life;
            c.a = ClampB((int)(40 + 180 * k));
        } break;
        default:
            c.r = ClampB(c.r + n / 12); c.g = ClampB(c.g + n / 12); c.b = ClampB(c.b + n / 12);
            break;
    }

    if (!MATERIALS[mat].emissive) {
        // Darken with depth (deeper = dimmer) and keep below the bloom gate.
        float df = 1.0f - wy * 0.00045f;
        if (df > 1.0f) df = 1.0f; if (df < 0.35f) df = 0.35f;
        c.r = (unsigned char)(c.r * df);
        c.g = (unsigned char)(c.g * df);
        c.b = (unsigned char)(c.b * df);
        c = CapBrightness(c, 195);
    }
    return c;
}

void GridDrawRipples(const Grid *g) {
    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &g->rRipples[i];
        if (!r->active) continue;
        DrawCircleLines((int)r->x, (int)r->y, r->radius,
                        Fade((Color){180, 220, 255, 255}, r->life * 0.6f));
    }
}

// Fast path: one colour per cell into a pixel buffer; the renderer uploads it
// as a single texture and draws ONE scaled quad. Only the camera-visible rect
// (plus margin) is recomputed each frame; offscreen texels keep their last
// colour and are clipped by the GPU anyway. When zoomed far out, cells are
// sub-pixel, so a flat per-material colour replaces the animated effects.
void GridFillPixels(const Grid *g, Color *out, Camera2D camera) {
    float t = (float)GetTime();

    Vector2 tl = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 br = GetScreenToWorld2D((Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, camera);
    int x0 = (int)(tl.x / CELL_SIZE) - 2 - g->rOriginX, y0 = (int)(tl.y / CELL_SIZE) - 2 - g->rOriginY;
    int x1 = (int)(br.x / CELL_SIZE) + 2 - g->rOriginX, y1 = (int)(br.y / CELL_SIZE) + 2 - g->rOriginY;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > g->width)  x1 = g->width;
    if (y1 > g->height) y1 = g->height;

    if (camera.zoom >= 0.45f) {
        for (int y = y0; y < y1; y++) {
            int i = y * g->width + x0;
            for (int x = x0; x < x1; x++, i++)
                out[i] = (g->rCells[i] == CELL_EMPTY) ? (Color){0, 0, 0, 0}
                                                      : CellColor(g, x, y, i, t);
        }
        return;
    }

    // Far zoom: flat colours (pre-capped per material) + per-row depth fade.
    Color flat[CELL_COUNT];
    for (int m = 0; m < CELL_COUNT; m++)
        flat[m] = MATERIALS[m].emissive ? MATERIALS[m].color
                                        : CapBrightness(MATERIALS[m].color, 195);
    for (int y = y0; y < y1; y++) {
        float df = 1.0f - (g->rOriginY + y) * 0.00045f;
        if (df > 1.0f) df = 1.0f; if (df < 0.35f) df = 0.35f;
        int i = y * g->width + x0;
        for (int x = x0; x < x1; x++, i++) {
            Cell mat = g->rCells[i];
            if (mat == CELL_EMPTY) { out[i] = (Color){0, 0, 0, 0}; continue; }
            Color c = flat[mat];
            if (!MATERIALS[mat].emissive) {
                c.r = (unsigned char)(c.r * df);
                c.g = (unsigned char)(c.g * df);
                c.b = (unsigned char)(c.b * df);
            }
            out[i] = c;
        }
    }
}

// Per-rectangle path, kept for the editor's small canvas.
void GridDrawWorld(const Grid *g, Camera2D camera) {
    float t = (float)GetTime();
    int originX = g->rOriginX, originY = g->rOriginY;

    Vector2 tl = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 br = GetScreenToWorld2D((Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, camera);
    int x0 = (int)(tl.x / CELL_SIZE) - 1 - originX, y0 = (int)(tl.y / CELL_SIZE) - 1 - originY;
    int x1 = (int)(br.x / CELL_SIZE) + 1 - originX, y1 = (int)(br.y / CELL_SIZE) + 1 - originY;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > g->width)  x1 = g->width;
    if (y1 > g->height) y1 = g->height;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int i = Idx(g, x, y);
            if (g->rCells[i] == CELL_EMPTY) continue;
            DrawRectangle((originX + x) * CELL_SIZE, (originY + y) * CELL_SIZE,
                          CELL_SIZE, CELL_SIZE, CellColor(g, x, y, i, t));
        }
    }
    GridDrawRipples(g);
}
