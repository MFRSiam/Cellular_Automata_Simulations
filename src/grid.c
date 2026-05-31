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
    g.cave = (CaveParams){ .scale = 0.045f, .threshold = 0.5f, .mudThreshold = 0.55f,
                           .octaves = 4, .seed = 1337, .biomes = true };
    return g;
}

void GridFree(Grid *g) {
    free(g->cells); free(g->flow); free(g->life); free(g->updated);
    free(g->sCells); free(g->sFlow); free(g->sLife);
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

static bool StepFire(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    bool quenched = false;
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) { GridSet(g, ax, ay, CELL_VAPOR); quenched = true; } // water -> vapor
        else if (c == CELL_ICE) { GridSet(g, ax, ay, CELL_WATER); }               // ice melts
        else if (Flammable(c)) {
            // Oil & gas catch readily and burn up fast; solids smoulder slowly.
            int chance = (c == CELL_OIL || c == CELL_GAS) ? 140 : 25;
            if (GetRandomValue(0, 1000) < chance) Ignite(g, ax, ay);
        }
    }
    if (quenched)        { GridSet(g, x, y, CELL_SMOKE); return true; }
    if (g->life[i] == 0) { GridSet(g, x, y, GetRandomValue(0, 2) == 0 ? CELL_EMPTY : CELL_SMOKE); return true; }
    g->life[i]--;
    StepGas(g, x, y, CELL_FIRE);
    return true;
}

static bool StepAcid(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        CellType t = TypeOf(c);
        // Eats solids/powders, but acid-proof materials (glass, metal,
        // obsidian) contain it instead.
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && !MATERIALS[c].acidProof &&
            GetRandomValue(0, 100) < 8) {
            GridSet(g, ax, ay, CELL_EMPTY);
            if (GetRandomValue(0, 100) < 30) { GridSet(g, x, y, CELL_EMPTY); return true; }
        }
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
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER || c == CELL_VAPOR) {        // quenched -> black stone
            GridSet(g, ax, ay, CELL_VAPOR);
            GridSet(g, x, y, CELL_BASALT);
            SpawnRipple(g, x, y);
            return true;
        }
        if (c == CELL_ICE)  { GridSet(g, ax, ay, CELL_WATER); }          // melt ice
        if (c == CELL_SAND) { GridSet(g, ax, ay, CELL_MOLTEN_GLASS); }   // sand -> molten glass
        // Lava slowly melts ordinary rock back into lava (not metal/obsidian).
        if ((c == CELL_ROCK || c == CELL_MUD || c == CELL_SANDSTONE || c == CELL_BASALT) &&
            GetRandomValue(0, 1000) < 3) GridSet(g, ax, ay, CELL_LAVA);
        if (Flammable(c) && GetRandomValue(0, 1000) < 40) Ignite(g, ax, ay);
    }
    // Cools in air to obsidian (a dark glass).
    if (GetRandomValue(0, 2000) < 2) { GridSet(g, x, y, CELL_OBSIDIAN); return true; }
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
                case CELL_WATER:
                case CELL_OIL:   StepLiquid(g, x, y, c); break;
                case CELL_ACID:  if (!StepAcid(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_SNOW:  if (!StepSnow(g, x, y)) StepPowder(g, x, y, c); break;
                case CELL_FIRE:  StepFire(g, x, y); break;
                case CELL_VAPOR: if (!StepVapor(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_SMOKE: if (!StepSmoke(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_GAS:   if (!StepFlammableGas(g, x, y)) StepGas(g, x, y, c); break;
                case CELL_LAVA:  if (!StepLava(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_MOLTEN_GLASS: if (!StepMoltenGlass(g, x, y)) StepLiquid(g, x, y, c); break;
                case CELL_ICE:   StepIce(g, x, y); break;
                default: break; // rock / mud / glass / metal / etc. are static
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

// Is there solid rock at this world cell? Uses biome-specific frequency,
// roughness and openness so each biome's caves actually look different.
static bool IsWall(const CaveParams *p, const BiomeInfo *bi, int wx, int wy) {
    float scale = p->scale * (bi ? bi->scaleMul : 1.0f);
    int   oct   = ClampI(p->octaves + (bi ? bi->octaveDelta : 0), 1, 8);
    float thr   = p->threshold + (bi ? bi->opennessBias : 0.0f);
    return NoiseFbm(wx * scale, wy * scale, oct) >= thr;
}

static Cell TerrainAt(const CaveParams *p, int wx, int wy) {
    // 1) Hand-made structures take priority wherever they are placed.
    Cell s = StructureSampleAt(p->seed, wx, wy);
    if (s != CELL_EMPTY) return s;

    // 2) Pick the biome (with a dithered material near borders so two biomes
    //    interleave instead of meeting at a hard line).
    const BiomeInfo *bi = NULL;
    Biome b = BIOME_ROCKY;
    if (p->biomes) {
        b = BiomeAt(wx, wy);
        Biome bAlt = BiomeAt(wx + 4, wy + 4);
        if (b != bAlt && (CellRandom(wx, wy, p->seed ^ 0xABCDu) < 0.5f)) b = bAlt;
        bi = &BIOMES[b];
    }

    if (!IsWall(p, bi, wx, wy)) return CELL_EMPTY;

    // 3) Rare glowing gold veins inside the rock.
    if (CellRandom(wx, wy, p->seed) < 0.004f) return CELL_GOLD;

    // 4) Sandy biome: loose sand on the surface, solid sandstone underneath so
    //    it doesn't all collapse. "Surface" = open air within a few cells up.
    if (b == BIOME_SANDY) {
        bool surface = false;
        for (int k = 1; k <= 4 && !surface; k++)
            if (!IsWall(p, bi, wx, wy - k)) surface = true;
        return surface ? CELL_SAND : CELL_SANDSTONE;
    }

    float mud = NoiseFbm(wx * p->scale * 1.8f + 100.0f, wy * p->scale * 1.8f + 100.0f, 2);
    Cell wallA = bi ? bi->wallPrimary : CELL_ROCK;
    Cell wallB = bi ? bi->wallSecondary : CELL_MUD;
    return (mud > p->mudThreshold) ? wallB : wallA;
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

void GridDrawWorld(const Grid *g, Camera2D camera) {
    float t = (float)GetTime();
    Vector2 tl = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 br = GetScreenToWorld2D((Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, camera);
    int x0 = (int)(tl.x / CELL_SIZE) - 1 - g->originX, y0 = (int)(tl.y / CELL_SIZE) - 1 - g->originY;
    int x1 = (int)(br.x / CELL_SIZE) + 1 - g->originX, y1 = (int)(br.y / CELL_SIZE) + 1 - g->originY;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > g->width)  x1 = g->width;
    if (y1 > g->height) y1 = g->height;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int i = Idx(g, x, y);
            Cell mat = g->cells[i];
            if (mat == CELL_EMPTY) continue;
            Color c = MATERIALS[mat].color;
            int n = Hash(x, y) - 128;

            switch (mat) {
                case CELL_WATER: {
                    int s = (int)(sinf(x * 0.4f + y * 0.2f + t * 3.0f) * 18.0f);
                    c.g = ClampB(c.g + s); c.b = ClampB(c.b + s);
                    if (!GridInBounds(g, x, y - 1) || g->cells[Idx(g, x, y - 1)] != CELL_WATER) {
                        c.r = ClampB(c.r + 70); c.g = ClampB(c.g + 60); c.b = ClampB(c.b + 25);
                    }
                } break;
                case CELL_FIRE: {
                    float k = g->life[i] / (float)MATERIALS[CELL_FIRE].life;
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
                case CELL_SMOKE:
                case CELL_VAPOR: {
                    float k = g->life[i] / (float)MATERIALS[mat].life;
                    c.a = ClampB((int)(40 + 180 * k));
                } break;
                default:
                    c.r = ClampB(c.r + n / 12); c.g = ClampB(c.g + n / 12); c.b = ClampB(c.b + n / 12);
                    break;
            }

            if (!MATERIALS[mat].emissive) {
                // Darken with depth (deeper = dimmer) and keep below bloom.
                int worldY = g->originY + y;
                float df = 1.0f - worldY * 0.00045f;
                if (df > 1.0f) df = 1.0f; if (df < 0.35f) df = 0.35f;
                c.r = (unsigned char)(c.r * df);
                c.g = (unsigned char)(c.g * df);
                c.b = (unsigned char)(c.b * df);
                c = CapBrightness(c, 195);
            }

            DrawRectangle((g->originX + x) * CELL_SIZE, (g->originY + y) * CELL_SIZE,
                          CELL_SIZE, CELL_SIZE, c);
        }
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &g->ripples[i];
        if (!r->active) continue;
        DrawCircleLines((int)r->x, (int)r->y, r->radius,
                        Fade((Color){180, 220, 255, 255}, r->life * 0.6f));
    }
}
