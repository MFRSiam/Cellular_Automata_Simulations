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
    g.cave = (CaveParams){ .scale = 0.045f, .threshold = 0.5f, .mudThreshold = 0.55f,
                           .octaves = 4, .seed = 1337, .biomes = true };
    return g;
}

void GridFree(Grid *g) {
    free(g->cells); free(g->flow); free(g->life); free(g->updated);
    free(g->sCells); free(g->sFlow); free(g->sLife);
    free(g->rCells); free(g->rLife);
    StoreFree();
    *g = (Grid){0};
}

void GridClear(Grid *g) {
    size_t n = (size_t)g->width * g->height;
    memset(g->cells, 0, n * sizeof(Cell));
    memset(g->flow,  0, n * sizeof(int8_t));
    memset(g->life,  0, n * sizeof(uint8_t));
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
            int chance = (c == CELL_OIL || c == CELL_GAS) ? 120 : 18;
            if (GetRandomValue(0, 1000) < chance) Ignite(g, ax, ay);
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

static bool StepAcid(Grid *g, int x, int y) {
    int water = 0;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) water++;
        CellType t = TypeOf(c);
        // Eats solids/powders, but acid-proof materials (glass, metal,
        // obsidian) contain it instead.
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
            GetRandomValue(0, 100) < 8) {
            GridSet(g, ax, ay, CELL_EMPTY);
            if (GetRandomValue(0, 100) < 30) { GridSet(g, x, y, CELL_EMPTY); return true; }
        }
    }
    // Dilution: water neutralises acid into (harmless) water - the more water
    // touching it, the faster it dilutes. Pour water on acid to wash it away.
    if (water > 0 && GetRandomValue(0, 100) < water * 5) { GridSet(g, x, y, CELL_WATER); return true; }
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
        if (c == CELL_ICE)  { GridSet(g, ax, ay, CELL_WATER); }          // melt ice
        if (c == CELL_SAND || c == CELL_GLASS) { GridSet(g, ax, ay, CELL_MOLTEN_GLASS); } // (re)melt to glass
        if (c == CELL_WAX)  { GridSet(g, ax, ay, CELL_MOLTEN_WAX); }      // melt wax
        // Lava slowly melts ordinary rock back into lava (not metal/obsidian).
        if ((c == CELL_ROCK || c == CELL_MUD || c == CELL_SANDSTONE || c == CELL_BASALT) &&
            GetRandomValue(0, 1000) < 3) GridSet(g, ax, ay, CELL_LAVA);
        if (Flammable(c) && GetRandomValue(0, 1000) < 40) Ignite(g, ax, ay);
    }
    // Only cools to obsidian when its surface is exposed to air; lava sealed
    // inside its shell (e.g. a contained lava lake) stays molten.
    if (hasAir && GetRandomValue(0, 2000) < 2) { GridSet(g, x, y, CELL_OBSIDIAN); return true; }
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
    if (heat == 0 && cold >= 2 && GetRandomValue(0, 1000) < 5) { GridSet(g, x, y, CELL_ICE); return true; }
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
    if (!water || GetRandomValue(0, 1000) >= 4) return;
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
// acid, but airborne) and slowly dissipates. Caller still runs StepGas.
static bool StepAcidGas(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        CellType t = TypeOf(c);
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
            GetRandomValue(0, 100) < 3)
            GridSet(g, ax, ay, CELL_EMPTY);
    }
    if (GetRandomValue(0, 1000) < 2) { GridSet(g, x, y, CELL_EMPTY); return true; } // dissipate
    return false;
}

// Grass lives over mud: it climbs a little, and creeps sideways across mud
// surfaces (so it carpets the ground, including fresh mud left by worms).
#define GRASS_MAX 5
static void StepGrass(Grid *g, int x, int y) {
    if (GetRandomValue(0, 1000) >= 6) return;

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
    if (GetRandomValue(0, 1000) >= 4) return;
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
    if (water && sx >= 0 && GetRandomValue(0, 2000) < 1) GridSet(g, sx, sy, CELL_CORAL);
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
void GridUpdate(Grid *g) {
    static int frame = 0;
    frame++;
    memset(g->updated, 0, (size_t)g->width * g->height);
    bool ltr = (frame % 2 == 0);

    for (int y = g->height - 1; y >= 0; y--) {
        int startX = ltr ? 0 : g->width - 1;
        int endX   = ltr ? g->width : -1;
        int stepX  = ltr ? 1 : -1;
        for (int x = startX; x != endX; x += stepX) {
            int i = Idx(g, x, y);
            if (g->updated[i]) continue;
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
    }
    UpdateRipples(g);
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

// Openness field, built from layered noise at three scales so big structure and
// fine detail come from independent sources (cf. Minecraft's separated noises,
// Noita's organic warping):
//   * MACRO - low frequency, domain-warped: the large caverns and solid masses.
//   * MICRO - high frequency: fine roughness on the wall surfaces.
//   * MESO  - mid-frequency ridged noise: connected winding tunnels so caves
//             are almost never fully sealed (ridge lines of a field connect).
static bool IsOpen(const CaveParams *p, const BiomeInfo *bi, int wx, int wy) {
    float s   = p->scale * (bi ? bi->scaleMul : 1.0f);
    int   oct = ClampI(p->octaves + (bi ? bi->octaveDelta : 0), 1, 8);
    float thr = p->threshold + (bi ? bi->opennessBias : 0.0f);

    // Domain warp the macro sample so big shapes swirl instead of looking blobby.
    float wf  = 26.0f;
    float wxx = wx + wf * NoisePerlin2(wx * s * 0.5f + 12.3f, wy * s * 0.5f + 4.1f);
    float wyy = wy + wf * NoisePerlin2(wx * s * 0.5f + 88.7f, wy * s * 0.5f + 51.9f);

    float macro = NoiseFbm(wxx * s, wyy * s, oct);                      // big structure
    float micro = NoiseFbm(wx * s * 4.0f + 71.0f, wy * s * 4.0f + 19.0f, 2); // fine detail
    float field = macro * 0.80f + micro * 0.20f;
    if (field < thr) return true;                                       // open room

    float r = NoiseFbm(wx * s * 0.75f + 41.3f, wy * s * 0.75f + 17.7f, ClampI(oct - 1, 1, 8));
    float ridge = 1.0f - fabsf(r * 2.0f - 1.0f);                        // ridged
    return ridge > 0.86f;                                              // connected tunnel
}
static bool IsWall(const CaveParams *p, const BiomeInfo *bi, int wx, int wy) {
    return !IsOpen(p, bi, wx, wy);
}

// Distance (in cells, 1..maxd) to the nearest wall above / below an open cell,
// or 0 if none within range. Used for stalactites, floors and surfaces.
static int DistUp(const CaveParams *p, const BiomeInfo *bi, int wx, int wy, int maxd) {
    for (int d = 1; d <= maxd; d++) if (IsWall(p, bi, wx, wy - d)) return d;
    return 0;
}
static int DistDown(const CaveParams *p, const BiomeInfo *bi, int wx, int wy, int maxd) {
    for (int d = 1; d <= maxd; d++) if (IsWall(p, bi, wx, wy + d)) return d;
    return 0;
}

// Stalactite (ceiling) / stalagmite (floor) test: deterministic per-column
// length so the rock spikes form contiguous cones.
static bool IsSpike(const CaveParams *p, const BiomeInfo *bi, int wx, int wy, bool ceiling) {
    int d = ceiling ? DistUp(p, bi, wx, wy, 5) : DistDown(p, bi, wx, wy, 5);
    if (d == 0) return false;
    unsigned h = (unsigned)(wx * 374761393) ^ (ceiling ? 0x1111u : 0x2222u) ^ (p->seed * 9176u);
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    if ((h & 7u) >= 3u) return false;       // only ~3/8 columns grow a spike
    int len = 1 + (int)((h >> 8) % 4u);     // 1..4 cells long
    return d <= len;
}

// Lined liquid bodies. A pool is the interior of a noise blob {v > hi}; the
// ring {hi-shell < v <= hi} forms a CLOSED shell of solid containment around
// it (the level set of a smooth field is a closed curve), so the liquid is
// sealed in and won't leak/flood unless the player digs into it.
//
// Returns true and sets *out to the interior liquid or a shell solid; false if
// this cell isn't part of this pool kind. shellA is the main lining, shellB a
// sparse accent. The shell material is chosen so the liquid can't escape:
//   water -> rock / wood        lava -> obsidian / copper (lava can't melt them)
//   acid  -> glass / metal (both acid-proof)
static bool PoolBand(int wx, int wy, float freq, float ox, float oy,
                     float hi, float shell, Cell liquid, Cell shellA, Cell shellB,
                     unsigned int seed, Cell *out) {
    float v = NoiseFbm(wx * freq + ox, wy * freq + oy, 3);
    if (v > hi) { *out = liquid; return true; }                 // interior
    if (v > hi - shell) {                                       // containing shell
        *out = (CellRandom(wx, wy, seed ^ 0x9E37u) < 0.18f) ? shellB : shellA;
        return true;
    }
    return false;
}

static bool PoolAt(const CaveParams *p, const BiomeInfo *bi, Biome b, int wx, int wy, Cell *out) {
    (void)bi;
    int depth = wy;
    unsigned int s = p->seed;

    // Coral reef: riddled with water pockets walled in by coral, so the whole
    // biome reads as a flooded reef without water leaking into its neighbours.
    if (b == BIOME_CORAL &&
        PoolBand(wx, wy, 0.050f, 33.0f, 17.0f, 0.60f, 0.07f,
                 CELL_WATER, CELL_CORAL, CELL_SAND, s ^ 0xC0DEu, out)) return true;

    // Ice lakes (cold biome): solid, so no shell needed.
    if (b == BIOME_COLD) {
        float v = NoiseFbm(wx * 0.05f + 5.0f, wy * 0.05f + 9.0f, 3);
        if (v > 0.74f) { *out = CELL_ICE; return true; }
    }
    // Acid pools - glass/metal shell. Mid depth, uncommon.
    if (depth > 40 && depth < 320 &&
        PoolBand(wx, wy, 0.060f, 70.0f, 30.0f, 0.85f, 0.08f,
                 CELL_ACID, CELL_GLASS, CELL_METAL, s ^ 0xAC1Du, out)) return true;
    // Lava lakes - obsidian/copper shell. Deep only.
    if (depth > 250 &&
        PoolBand(wx, wy, 0.045f, 200.0f, 150.0f, 0.82f, 0.07f,
                 CELL_LAVA, CELL_OBSIDIAN, CELL_COPPER, s ^ 0x1A33u, out)) return true;
    // Huge oil reservoirs encased in a thick wooden shell - scattered all over
    // (big and low-frequency). Torch the wood and the whole thing goes up.
    if (PoolBand(wx, wy, 0.030f, 600.0f, 250.0f, 0.78f, 0.07f,
                 CELL_OIL, CELL_WOOD, CELL_WOOD, s ^ 0x0117u, out)) return true;

    // Water lakes / underwater pockets - rock/wood shell. Any depth.
    if (PoolBand(wx, wy, 0.050f, 400.0f, 88.0f, 0.80f, 0.06f,
                 CELL_WATER, CELL_ROCK, CELL_WOOD, s ^ 0x7711u, out)) return true;
    return false;
}

// Material for a solid (wall) cell.
static Cell WallMaterial(const CaveParams *p, const BiomeInfo *bi, Biome b, int wx, int wy) {
    bool voidB = (b == BIOME_VOID);

    // Rare clustered gold chambers (rarer outside the Void).
    float chamber = NoiseFbm(wx * 0.02f + p->seed * 0.013f + 311.0f,
                             wy * 0.02f + p->seed * 0.007f + 733.0f, 2);
    if (chamber > (voidB ? 0.82f : 0.92f)) return CELL_GOLD;
    if (CellRandom(wx, wy, p->seed) < 0.003f) return CELL_GOLD;   // sparse flecks

    if (voidB) {
        // Out-of-this-world: a dark obsidian shell veined with shiny copper and
        // studded with glowing crystals.
        if (CellRandom(wx, wy, p->seed ^ 0x51A3u) < 0.05f) return CELL_CRYSTAL;
        float vein = NoiseFbm(wx * p->scale * 1.6f + 200.0f, wy * p->scale * 1.6f + 90.0f, 2);
        if (vein > 0.62f) return CELL_COPPER;
        return CELL_OBSIDIAN;
    }

    if (b == BIOME_SANDY) { // loose sand surface over solid sandstone
        bool surface = false;
        for (int k = 1; k <= 4 && !surface; k++)
            if (IsOpen(p, bi, wx, wy - k)) surface = true;
        return surface ? CELL_SAND : CELL_SANDSTONE;
    }

    // Coal seams threaded through ordinary rock (fuel for fires deep down).
    float coal = NoiseFbm(wx * p->scale * 1.5f + 501.0f, wy * p->scale * 1.5f + 87.0f, 2);
    if (coal > 0.74f) return CELL_COAL;

    float mud = NoiseFbm(wx * p->scale * 1.8f + 100.0f, wy * p->scale * 1.8f + 100.0f, 2);
    Cell wallA = bi ? bi->wallPrimary : CELL_ROCK;
    Cell wallB = bi ? bi->wallSecondary : CELL_MUD;
    return (mud > p->mudThreshold) ? wallB : wallA;
}

static Cell TerrainAt(const CaveParams *p, int wx, int wy) {
    // 1) Hand-made structures take priority wherever they are placed.
    Cell s = StructureSampleAt(p->seed, wx, wy);
    if (s != CELL_EMPTY) return s;

    // 2) Pick the biome. Near a border, dither against the neighbouring biome
    //    using a smooth noise so the two interleave organically (no hard line).
    const BiomeInfo *bi = NULL;
    Biome b = BIOME_ROCKY;
    if (p->biomes) {
        b = BiomeAt(wx, wy);
        Biome bAlt = BiomeAt(wx + 5, wy + 3);
        if (b != bAlt && NoiseFbm(wx * 0.05f + 9.0f, wy * 0.05f + 4.0f, 2) < 0.5f) b = bAlt;
        bi = &BIOMES[b];
    }

    // 3) Contained liquid bodies (lakes/pools) override the normal cave so
    //    their solid shell always seals the liquid - it stays put untouched.
    Cell pc;
    if (PoolAt(p, bi, b, wx, wy, &pc)) return pc;

    if (IsOpen(p, bi, wx, wy)) {
        // Rock spikes hanging from ceilings / rising from floors.
        if (IsSpike(p, bi, wx, wy, true) || IsSpike(p, bi, wx, wy, false)) return CELL_ROCK;

        // Living surfaces: grass carpets mud floors; vines drape jungle ceilings.
        if ((b == BIOME_JUNGLE || b == BIOME_OPEN) && DistDown(p, bi, wx, wy, 1) == 1) {
            if (WallMaterial(p, bi, b, wx, wy + 1) == CELL_MUD &&
                CellRandom(wx, wy, p->seed ^ 0x6Eu) < 0.45f)
                return CELL_GRASS;
        }
        if (b == BIOME_JUNGLE && DistUp(p, bi, wx, wy, 1) == 1 &&
            CellRandom(wx, wy, p->seed ^ 0x71u) < 0.30f)
            return CELL_VINE;

        return CELL_EMPTY;
    }

    return WallMaterial(p, bi, b, wx, wy);
}

void GridRegenerate(Grid *g) {
    StoreClear(); // a fresh layout: forget previous edits
    NoiseInit(g->cave.seed);
    for (int y = 0; y < g->height; y++)
        for (int x = 0; x < g->width; x++)
            GridSet(g, x, y, TerrainAt(&g->cave, g->originX + x, g->originY + y));
    for (int i = 0; i < MAX_RIPPLES; i++) g->ripples[i].active = false;
}

void GridStreamTo(Grid *g, int nox, int noy) {
    int dx = nox - g->originX, dy = noy - g->originY;
    if (dx == 0 && dy == 0) return;
    int w = g->width, h = g->height;

    // 1) Save cells that are about to leave the window so they persist.
    for (int oy = 0; oy < h; oy++) {
        for (int ox = 0; ox < w; ox++) {
            int nx = ox - dx, ny = oy - dy; // where this cell lands in the new window
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                int oi = oy * w + ox;
                StoreSet(g->originX + ox, g->originY + oy, g->cells[oi], g->life[oi]);
            }
        }
    }

    // 2) Build the shifted window; newly exposed cells come from the store
    //    (persisted edits) or, failing that, freshly generated terrain.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int di = y * w + x, sx = x + dx, sy = y + dy;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                int si = sy * w + sx;
                g->sCells[di] = g->cells[si]; g->sFlow[di] = g->flow[si]; g->sLife[di] = g->life[si];
            } else {
                int wx = nox + x, wy = noy + y;
                Cell c; uint8_t lf;
                if (StoreGet(wx, wy, &c, &lf)) { g->sCells[di] = c; g->sLife[di] = lf; }
                else { c = TerrainAt(&g->cave, wx, wy); g->sCells[di] = c; g->sLife[di] = MATERIALS[c].life; }
                g->sFlow[di] = 0;
            }
        }
    }
    size_t n = (size_t)w * h;
    memcpy(g->cells, g->sCells, n * sizeof(Cell));
    memcpy(g->flow,  g->sFlow,  n * sizeof(int8_t));
    memcpy(g->life,  g->sLife,  n * sizeof(uint8_t));
    g->originX = nox; g->originY = noy;
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

void GridDrawWorld(const Grid *g, Camera2D camera) {
    float t = (float)GetTime();
    // Read from the render snapshot, not the live arrays (the worker thread may
    // be mutating those concurrently).
    const Cell    *cells   = g->rCells;
    const uint8_t *lifeArr = g->rLife;
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
            Cell mat = cells[i];
            if (mat == CELL_EMPTY) continue;
            Color c = MATERIALS[mat].color;
            int n = Hash(x, y) - 128;

            switch (mat) {
                case CELL_WATER: {
                    int s = (int)(sinf(x * 0.4f + y * 0.2f + t * 3.0f) * 18.0f);
                    c.g = ClampB(c.g + s); c.b = ClampB(c.b + s);
                    if (!GridInBounds(g, x, y - 1) || cells[Idx(g, x, y - 1)] != CELL_WATER) {
                        c.r = ClampB(c.r + 70); c.g = ClampB(c.g + 60); c.b = ClampB(c.b + 25);
                    }
                } break;
                case CELL_FIRE: {
                    float k = lifeArr[i] / (float)MATERIALS[CELL_FIRE].life;
                    int f = GetRandomValue(-25, 25);
                    c.r = 255; c.g = ClampB((int)(90 + 120 * k) + f); c.b = ClampB((int)(20 * k) + f / 2);
                } break;
                case CELL_LAVA: {
                    float glow = sinf(x * 0.5f + y * 0.5f + t * 4.0f) * 0.5f + 0.5f;
                    c.r = ClampB(220 + (int)(35 * glow));
                    c.g = ClampB(60 + (int)(90 * glow) + n / 6);
                    c.b = ClampB(10 + (int)(20 * glow));
                } break;
                case CELL_GOLD: {
                    float pulse = sinf(t * 2.5f + (x + y) * 0.6f) * 0.5f + 0.5f;
                    c.r = ClampB(c.r + (int)(20 * pulse));
                    c.g = ClampB(c.g + (int)(30 * pulse));
                } break;
                case CELL_MOLTEN_GLASS: {
                    float glow = sinf(x * 0.6f + y * 0.4f + t * 5.0f) * 0.5f + 0.5f;
                    c.r = 255;
                    c.g = ClampB(150 + (int)(80 * glow));
                    c.b = ClampB(40 + (int)(40 * glow));
                } break;
                case CELL_CRYSTAL: {
                    // Glowing gemstone: pulsing cool light (emissive -> blooms).
                    float pulse = sinf(t * 1.8f + (x * 0.7f - y * 0.5f)) * 0.5f + 0.5f;
                    c.r = ClampB(120 + (int)(60 * pulse));
                    c.g = ClampB(190 + (int)(50 * pulse));
                    c.b = 255;
                } break;
                case CELL_COPPER: {
                    // Metallic sheen: a moving specular streak across the ore.
                    float spec = sinf(x * 0.9f + y * 0.5f + t * 1.5f);
                    int s = (int)(fmaxf(spec, 0.0f) * 55.0f);
                    c.r = ClampB(c.r + s); c.g = ClampB(c.g + s * 3 / 4); c.b = ClampB(c.b + s / 2);
                } break;
                case CELL_MERCURY: {
                    // Liquid-metal silver with a sliding specular highlight.
                    float spec = sinf(x * 0.8f + y * 0.4f + t * 2.0f);
                    int s = (int)(fmaxf(spec, 0.0f) * 60.0f);
                    c.r = ClampB(c.r + s); c.g = ClampB(c.g + s); c.b = ClampB(c.b + s);
                } break;
                case CELL_SPARK: {
                    // Crackling electric blue-white (emissive -> blooms).
                    int f = GetRandomValue(-45, 45);
                    c.r = ClampB(190 + f); c.g = ClampB(225 + f); c.b = 255;
                } break;
                case CELL_MOLTEN_WAX: {
                    float glow = sinf(x * 0.5f + y * 0.3f + t * 3.0f) * 0.5f + 0.5f;
                    c.r = ClampB(c.r + (int)(25 * glow));
                    c.g = ClampB(c.g + (int)(18 * glow));
                } break;
                case CELL_CORAL: {
                    // Multi-colour reef: pick a hue per cell for a vivid look.
                    switch (Hash(x * 3, y * 7) & 3) {
                        case 0: c = (Color){255, 110, 150, 255}; break; // pink
                        case 1: c = (Color){255, 150,  80, 255}; break; // orange
                        case 2: c = (Color){180, 110, 220, 255}; break; // purple
                        default:c = (Color){ 90, 210, 200, 255}; break; // teal
                    }
                    c.r = ClampB(c.r + n / 14); c.g = ClampB(c.g + n / 14); c.b = ClampB(c.b + n / 14);
                } break;
                case CELL_SMOKE:
                case CELL_VAPOR: {
                    float k = lifeArr[i] / (float)MATERIALS[mat].life;
                    c.a = ClampB((int)(40 + 180 * k));
                } break;
                default:
                    c.r = ClampB(c.r + n / 12); c.g = ClampB(c.g + n / 12); c.b = ClampB(c.b + n / 12);
                    break;
            }

            if (!MATERIALS[mat].emissive) {
                // Darken with depth (deeper = dimmer) and keep below bloom.
                int worldY = originY + y;
                float df = 1.0f - worldY * 0.00045f;
                if (df > 1.0f) df = 1.0f; if (df < 0.35f) df = 0.35f;
                c.r = (unsigned char)(c.r * df);
                c.g = (unsigned char)(c.g * df);
                c.b = (unsigned char)(c.b * df);
                c = CapBrightness(c, 195);
            }

            DrawRectangle((originX + x) * CELL_SIZE, (originY + y) * CELL_SIZE,
                          CELL_SIZE, CELL_SIZE, c);
        }
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &g->rRipples[i];
        if (!r->active) continue;
        DrawCircleLines((int)r->x, (int)r->y, r->radius,
                        Fade((Color){180, 220, 255, 255}, r->life * 0.6f));
    }
}
