//
// Cellular Automata: a falling-sand / fluid sandbox. See Application.h.
//

#include "Application.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Material table. Density drives gravity & buoyancy: heavier values sink
// below lighter ones, air is 0, and gases are negative so they rise.
// ---------------------------------------------------------------------------
static const MatInfo MATERIALS[CELL_COUNT] = {
    [CELL_EMPTY] = {"Eraser", {0, 0, 0, 0},            TYPE_EMPTY,    0, false,   0},
    [CELL_SAND]  = {"Sand",   {222, 184, 110, 255},    TYPE_POWDER, 200, false,   0},
    [CELL_WATER] = {"Water",  { 64, 128, 220, 255},    TYPE_LIQUID, 100, false,   0},
    [CELL_WOOD]  = {"Wood",   {112,  74,  40, 255},    TYPE_SOLID,  500, true,    0},
    [CELL_OIL]   = {"Oil",    {120,  90,  40, 255},    TYPE_LIQUID,  60, true,    0},
    [CELL_ACID]  = {"Acid",   {120, 220,  60, 255},    TYPE_LIQUID, 120, false,   0},
    [CELL_SNOW]  = {"Snow",   {235, 240, 255, 255},    TYPE_POWDER,  40, false,   0},
    [CELL_FIRE]  = {"Fire",   {255, 140,  30, 255},    TYPE_GAS,    -15, false,  70},
    [CELL_SMOKE] = {"Smoke",  { 60,  60,  68, 255},    TYPE_GAS,    -25, false, 180},
    [CELL_VAPOR] = {"Vapor",  {205, 215, 230, 255},    TYPE_GAS,    -10, false, 255},
    [CELL_GAS]   = {"Gas",    {150, 220, 120, 255},    TYPE_GAS,     -5, true,    0},
    [CELL_LAVA]  = {"Lava",   {255, 100,  20, 255},    TYPE_LIQUID, 250, false,   0},
    [CELL_ROCK]  = {"Rock",   { 90,  92, 100, 255},    TYPE_SOLID,  900, false,   0},
    [CELL_MUD]   = {"Mud",    { 86,  64,  44, 255},    TYPE_SOLID,  800, false,   0},
};

static inline CellType TypeOf(Cell c)  { return MATERIALS[c].type; }
static inline int      DensityOf(Cell c){ return MATERIALS[c].density; }
static inline bool     Flammable(Cell c){ return MATERIALS[c].flammable; }
static inline int      Idx(const Grid *g, int x, int y) { return y * g->width + x; }

// ---------------------------------------------------------------------------
// Grid lifecycle
// ---------------------------------------------------------------------------
Grid GridCreate(int windowWidth, int windowHeight) {
    Grid grid = {
        .width  = windowWidth / CELL_SIZE,
        .height = windowHeight / CELL_SIZE,
    };
    size_t count = (size_t)grid.width * grid.height;
    grid.cells   = calloc(count, sizeof(Cell));
    grid.flow    = calloc(count, sizeof(int8_t));
    grid.life    = calloc(count, sizeof(uint8_t));
    grid.updated = calloc(count, sizeof(uint8_t));
    grid.sCells  = calloc(count, sizeof(Cell));
    grid.sFlow   = calloc(count, sizeof(int8_t));
    grid.sLife   = calloc(count, sizeof(uint8_t));
    grid.cave = (CaveParams){
        .scale = 0.045f, .threshold = 0.50f, .mudThreshold = 0.55f,
        .octaves = 4, .seed = 1337,
    };
    return grid;
}

void GridFree(Grid *grid) {
    free(grid->cells);
    free(grid->flow);
    free(grid->life);
    free(grid->updated);
    free(grid->sCells);
    free(grid->sFlow);
    free(grid->sLife);
    *grid = (Grid){0};
}

void GridClear(Grid *grid) {
    size_t count = (size_t)grid->width * grid->height;
    memset(grid->cells, 0, count * sizeof(Cell));
    memset(grid->flow,  0, count * sizeof(int8_t));
    memset(grid->life,  0, count * sizeof(uint8_t));
}

bool GridInBounds(const Grid *grid, int x, int y) {
    return x >= 0 && x < grid->width && y >= 0 && y < grid->height;
}

Cell GridGet(const Grid *grid, int x, int y) {
    return grid->cells[Idx(grid, x, y)];
}

void GridSet(Grid *grid, int x, int y, Cell mat) {
    int i = Idx(grid, x, y);
    grid->cells[i] = mat;
    grid->flow[i]  = 0;
    grid->life[i]  = MATERIALS[mat].life;
}

void GridPaint(Grid *grid, int cx, int cy, int radius, Cell mat) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue; // circular brush
            if (!GridInBounds(grid, x, y)) continue;
            // Don't repaint over solids/fluids when laying down gases & fire so
            // a brush of fire doesn't erase the wood it should ignite.
            if (mat != CELL_EMPTY && GridGet(grid, x, y) != CELL_EMPTY &&
                TypeOf(mat) == TYPE_GAS) continue;
            GridSet(grid, x, y, mat);
        }
    }
}

// ---------------------------------------------------------------------------
// Movement primitives
// ---------------------------------------------------------------------------

// True if `mover` (sinking/falling) may enter (x,y): empty, or a *lighter*
// fluid it can push out of the way. Solids and powders block.
static bool CanSink(const Grid *g, Cell mover, int x, int y) {
    if (!GridInBounds(g, x, y)) return false;
    Cell t = GridGet(g, x, y);
    if (t == CELL_EMPTY) return true;
    CellType tt = TypeOf(t);
    return (tt == TYPE_LIQUID || tt == TYPE_GAS) && DensityOf(mover) > DensityOf(t);
}

// True if `mover` (rising gas) may enter (x,y): empty, or a *heavier* fluid.
static bool CanRise(const Grid *g, Cell mover, int x, int y) {
    if (!GridInBounds(g, x, y)) return false;
    Cell t = GridGet(g, x, y);
    if (t == CELL_EMPTY) return true;
    CellType tt = TypeOf(t);
    return (tt == TYPE_LIQUID || tt == TYPE_GAS) && DensityOf(mover) < DensityOf(t);
}

// Swap two cells (material + momentum + life) and mark both as processed so
// neither is stepped again this frame.
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

// Liquids fall, slide diagonally, then flow sideways with momentum: once
// moving in a direction they keep going until blocked, so streams look fluid
// and puddles level out. (Adapted from the raycast-dispersion approach.)
static void StepLiquid(Grid *g, int x, int y, Cell self) {
    int i = Idx(g, x, y);

    if (CanSink(g, self, x, y + 1)) { Move(g, x, y, x, y + 1); return; }

    int dir = g->flow[i] != 0 ? g->flow[i] : (GetRandomValue(0, 1) ? 1 : -1);
    for (int s = 0; s < 2; s++) {
        int dx = (s == 0) ? dir : -dir;
        if (CanSink(g, self, x + dx, y + 1)) {
            g->flow[i] = (int8_t)dx;
            Move(g, x, y, x + dx, y + 1);
            return;
        }
    }

    // Look ahead each way for open space and for a "cliff" (a spot it can fall
    // from). Prefer a cliff; otherwise keep our momentum direction.
    int maxL = 0, maxR = 0;
    bool cliffL = false, cliffR = false;
    for (int k = 1; k <= WATER_DISPERSION; k++) {
        if (!CanSink(g, self, x - k, y)) break;
        maxL = k;
        if (CanSink(g, self, x - k, y + 1)) { cliffL = true; break; }
    }
    for (int k = 1; k <= WATER_DISPERSION; k++) {
        if (!CanSink(g, self, x + k, y)) break;
        maxR = k;
        if (CanSink(g, self, x + k, y + 1)) { cliffR = true; break; }
    }

    if (maxL == 0 && maxR == 0) { g->flow[i] = 0; return; } // boxed in: rest

    int dx;
    if (cliffL && !cliffR)      dx = -maxL;
    else if (cliffR && !cliffL) dx =  maxR;
    else if (dir < 0 && maxL)   dx = -maxL;   // follow momentum
    else if (dir > 0 && maxR)   dx =  maxR;
    else                        dx = maxL ? -maxL : maxR; // momentum blocked

    g->flow[i] = (int8_t)(dx < 0 ? -1 : 1);
    Move(g, x, y, x + dx, y);
}

// Gases rise, drift diagonally up, and diffuse sideways into open air.
static void StepGas(Grid *g, int x, int y, Cell self) {
    if (CanRise(g, self, x, y - 1)) { Move(g, x, y, x, y - 1); return; }
    int first = GetRandomValue(0, 1) ? 1 : -1;
    for (int i = 0; i < 2; i++) {
        int dx = (i == 0) ? first : -first;
        if (CanRise(g, self, x + dx, y - 1)) { Move(g, x, y, x + dx, y - 1); return; }
    }
    // Diffusion: random sideways wander into empty space.
    for (int i = 0; i < 2; i++) {
        int dx = (i == 0) ? first : -first;
        if (GridInBounds(g, x + dx, y) && GridGet(g, x + dx, y) == CELL_EMPTY) {
            Move(g, x, y, x + dx, y);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Ripples
// ---------------------------------------------------------------------------
static void SpawnRipple(Grid *g, int cx, int cy) {
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!g->ripples[i].active) {
            g->ripples[i] = (Ripple){ // store in absolute world pixels
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
// Reactions. Each returns true if it consumed the cell (so movement is skipped).
// ---------------------------------------------------------------------------
static const int NX[4] = {0, 0, -1, 1};
static const int NY[4] = {-1, 1, 0, 0};

// Ignite a flammable neighbour, marking it done so it doesn't burn instantly.
static void Ignite(Grid *g, int x, int y) {
    GridSet(g, x, y, CELL_FIRE);
    g->updated[Idx(g, x, y)] = 1;
}

static bool StepFire(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    bool nearWater = false;

    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_WATER) nearWater = true;
        else if (Flammable(c) && GetRandomValue(0, 100) < 45) Ignite(g, ax, ay);
    }

    if (nearWater) {                       // doused -> puff of steam
        GridSet(g, x, y, CELL_VAPOR);
        return true;
    }
    if (g->life[i] == 0) {                  // burned out -> smoke
        GridSet(g, x, y, GetRandomValue(0, 2) == 0 ? CELL_EMPTY : CELL_SMOKE);
        return true;
    }
    g->life[i]--;
    StepGas(g, x, y, CELL_FIRE);            // flicker upward
    return true;
}

static bool StepAcid(Grid *g, int x, int y) {
    // Dissolve an adjacent solid/powder; the acid is sometimes used up.
    for (int n = 0; n < 4; n++) {
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        CellType t = TypeOf(GridGet(g, ax, ay));
        if ((t == TYPE_SOLID || t == TYPE_POWDER) && GetRandomValue(0, 100) < 8) {
            GridSet(g, ax, ay, CELL_EMPTY);
            if (GetRandomValue(0, 100) < 30) { GridSet(g, x, y, CELL_EMPTY); return true; }
        }
    }
    return false; // fall through to liquid motion
}

static bool StepSnow(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {           // melt next to anything hot
        int ax = x + NX[n], ay = y + NY[n];
        if (!GridInBounds(g, ax, ay)) continue;
        Cell c = GridGet(g, ax, ay);
        if (c == CELL_FIRE || c == CELL_VAPOR) { GridSet(g, x, y, CELL_WATER); return true; }
    }
    if (GetRandomValue(0, 2) == 0) return true; // flutter: skip some frames
    return false;
}

static bool StepVapor(Grid *g, int x, int y) {
    int i = Idx(g, x, y);
    bool atTop = !CanRise(g, CELL_VAPOR, x, y - 1); // can't go up: cooling
    if (g->life[i] == 0 || (atTop && GetRandomValue(0, 100) < 4)) {
        GridSet(g, x, y, CELL_WATER);       // condense
        SpawnRipple(g, x, y);
        return true;
    }
    g->life[i]--;
    return false; // fall through to gas motion
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
        if (c == CELL_WATER || c == CELL_VAPOR) {   // quench -> rock + steam
            GridSet(g, ax, ay, CELL_VAPOR);
            GridSet(g, x, y, CELL_ROCK);
            SpawnRipple(g, x, y);
            return true;
        }
        if (Flammable(c) && GetRandomValue(0, 100) < 25) Ignite(g, ax, ay);
    }
    if (GetRandomValue(0, 1500) < 2) {              // slowly crusts over to rock
        GridSet(g, x, y, CELL_ROCK);
        return true;
    }
    return false; // otherwise flow like a (thick) liquid
}

static bool StepFlammableGas(Grid *g, int x, int y) {
    for (int n = 0; n < 4; n++) {           // ignites on contact with fire
        int ax = x + NX[n], ay = y + NY[n];
        if (GridInBounds(g, ax, ay) && GridGet(g, ax, ay) == CELL_FIRE) {
            Ignite(g, x, y);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Frame step
// ---------------------------------------------------------------------------
void GridUpdate(Grid *grid) {
    static int frame = 0;
    frame++;
    memset(grid->updated, 0, (size_t)grid->width * grid->height);

    bool ltr = (frame % 2 == 0); // alternate scan direction for symmetry

    // Bottom-to-top so falling cells aren't processed twice.
    for (int y = grid->height - 1; y >= 0; y--) {
        int startX = ltr ? 0 : grid->width - 1;
        int endX   = ltr ? grid->width : -1;
        int stepX  = ltr ? 1 : -1;

        for (int x = startX; x != endX; x += stepX) {
            int i = Idx(grid, x, y);
            if (grid->updated[i]) continue;
            Cell c = grid->cells[i];

            switch (c) {
                case CELL_SAND:  StepPowder(grid, x, y, c); break;
                case CELL_WOOD:  break; // static (burns via fire)

                case CELL_WATER: StepLiquid(grid, x, y, c); break;
                case CELL_OIL:   StepLiquid(grid, x, y, c); break;
                case CELL_ACID:  if (!StepAcid(grid, x, y)) StepLiquid(grid, x, y, c); break;
                case CELL_SNOW:  if (!StepSnow(grid, x, y)) StepPowder(grid, x, y, c); break;

                case CELL_FIRE:  StepFire(grid, x, y); break;
                case CELL_VAPOR: if (!StepVapor(grid, x, y)) StepGas(grid, x, y, c); break;
                case CELL_SMOKE: if (!StepSmoke(grid, x, y)) StepGas(grid, x, y, c); break;
                case CELL_GAS:   if (!StepFlammableGas(grid, x, y)) StepGas(grid, x, y, c); break;
                case CELL_LAVA:  if (!StepLava(grid, x, y)) StepLiquid(grid, x, y, c); break;

                default: break; // rock & mud are static
            }
        }
    }
    UpdateRipples(grid);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
const char *CellName(Cell mat) { return MATERIALS[mat].name; }

static unsigned char ClampByte(int v) {
    return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// Cheap per-cell pseudo-random value (0..255) for static texture.
static int Hash(int x, int y) {
    int h = x * 374761393 + y * 668265263;
    h = (h ^ (h >> 13)) * 1274126177;
    return (h ^ (h >> 16)) & 0xFF;
}

void GridDraw(const Grid *grid, Camera2D camera) {
    float t = (float)GetTime();

    // Cull to the visible region (absolute world cells -> buffer indices).
    Vector2 tl = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 br = GetScreenToWorld2D(
        (Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, camera);
    int x0 = (int)(tl.x / CELL_SIZE) - 1 - grid->originX;
    int y0 = (int)(tl.y / CELL_SIZE) - 1 - grid->originY;
    int x1 = (int)(br.x / CELL_SIZE) + 1 - grid->originX;
    int y1 = (int)(br.y / CELL_SIZE) + 1 - grid->originY;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > grid->width)  x1 = grid->width;
    if (y1 > grid->height) y1 = grid->height;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int i = Idx(grid, x, y);
            Cell mat = grid->cells[i];
            if (mat == CELL_EMPTY) continue;

            Color c = MATERIALS[mat].color;
            int n = Hash(x, y) - 128; // -128..127

            switch (mat) {
                case CELL_WATER: {
                    int s = (int)(sinf(x * 0.4f + y * 0.2f + t * 3.0f) * 18.0f);
                    c.g = ClampByte(c.g + s);
                    c.b = ClampByte(c.b + s);
                    if (!GridInBounds(grid, x, y - 1) || grid->cells[Idx(grid, x, y - 1)] != CELL_WATER) {
                        c.r = ClampByte(c.r + 70); c.g = ClampByte(c.g + 60); c.b = ClampByte(c.b + 25);
                    }
                } break;
                case CELL_FIRE: {
                    // Hotter (whiter/brighter) while young, redder as it dies.
                    float k = grid->life[i] / (float)MATERIALS[CELL_FIRE].life;
                    int flick = GetRandomValue(-25, 25);
                    c.r = ClampByte(255);
                    c.g = ClampByte((int)(90 + 120 * k) + flick);
                    c.b = ClampByte((int)(20 * k) + flick / 2);
                } break;
                case CELL_LAVA: {
                    // Glowing molten surface: bright veins over a darker base.
                    float glow = sinf(x * 0.5f + y * 0.5f + t * 4.0f) * 0.5f + 0.5f;
                    c.r = ClampByte(220 + (int)(35 * glow));
                    c.g = ClampByte(60 + (int)(90 * glow) + n / 6);
                    c.b = ClampByte(10 + (int)(20 * glow));
                } break;
                case CELL_SMOKE:
                case CELL_VAPOR: {
                    float k = grid->life[i] / (float)MATERIALS[mat].life;
                    c.a = ClampByte((int)(40 + 180 * k)); // fade as it dissipates
                } break;
                case CELL_SAND:
                case CELL_SNOW:
                case CELL_WOOD:
                case CELL_OIL:
                case CELL_ACID:
                    c.r = ClampByte(c.r + n / 8);
                    c.g = ClampByte(c.g + n / 8);
                    c.b = ClampByte(c.b + n / 8);
                    break;
                default: break;
            }
            DrawRectangle((grid->originX + x) * CELL_SIZE,
                          (grid->originY + y) * CELL_SIZE,
                          CELL_SIZE, CELL_SIZE, c);
        }
    }

    for (int i = 0; i < MAX_RIPPLES; i++) {
        const Ripple *r = &grid->ripples[i];
        if (!r->active) continue;
        DrawCircleLines((int)r->x, (int)r->y, r->radius,
                        Fade((Color){180, 220, 255, 255}, r->life * 0.6f));
    }
}

// ---------------------------------------------------------------------------
// Perlin noise (Ken Perlin's improved 2D gradient noise). Self-contained so
// terrain can be sampled at any absolute world coordinate on demand, which is
// what lets the cave stream endlessly as the camera pans.
// ---------------------------------------------------------------------------
static int s_perm[512];
static unsigned int s_permSeed = 0xFFFFFFFFu; // forces first init

static void PerlinInit(unsigned int seed) {
    int p[256];
    for (int i = 0; i < 256; i++) p[i] = i;
    unsigned int s = seed ? seed : 1; // xorshift shuffle
    for (int i = 255; i > 0; i--) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int j = (int)(s % (unsigned)(i + 1));
        int t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (int i = 0; i < 256; i++) s_perm[i] = s_perm[i + 256] = p[i];
    s_permSeed = seed;
}

static float PFade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static float PLerp(float a, float b, float t) { return a + t * (b - a); }
static float PGrad(int h, float x, float y) {
    h &= 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

static float Perlin2(float x, float y) {
    int X = (int)floorf(x) & 255, Y = (int)floorf(y) & 255;
    x -= floorf(x); y -= floorf(y);
    float u = PFade(x), v = PFade(y);
    int A = s_perm[X] + Y, B = s_perm[X + 1] + Y;
    return PLerp(PLerp(PGrad(s_perm[A],     x,     y),     PGrad(s_perm[B],     x - 1, y),     u),
                 PLerp(PGrad(s_perm[A + 1], x,     y - 1), PGrad(s_perm[B + 1], x - 1, y - 1), u), v);
}

// Fractal Brownian motion: sum octaves of Perlin noise. Returns ~0..1.
static float Fbm(float x, float y, int octaves) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum  += amp * Perlin2(x * freq, y * freq);
        norm += amp;
        freq *= 2.0f;
        amp  *= 0.5f;
    }
    return (sum / norm) * 0.5f + 0.5f;
}

// What material lives at an absolute world cell, purely from the params.
static Cell TerrainAt(const CaveParams *p, int wx, int wy) {
    float nx = wx * p->scale, ny = wy * p->scale;
    if (Fbm(nx, ny, p->octaves) < p->threshold) return CELL_EMPTY; // open cave
    float mud = Fbm(nx * 1.8f + 100.0f, ny * 1.8f + 100.0f, 2);
    return (mud > p->mudThreshold) ? CELL_MUD : CELL_ROCK;
}

void GridRegenerate(Grid *grid) {
    if (grid->cave.seed != s_permSeed) PerlinInit(grid->cave.seed);
    for (int y = 0; y < grid->height; y++)
        for (int x = 0; x < grid->width; x++)
            GridSet(grid, x, y, TerrainAt(&grid->cave, grid->originX + x, grid->originY + y));
    for (int i = 0; i < MAX_RIPPLES; i++) grid->ripples[i].active = false;
}

void GridStreamTo(Grid *grid, int nox, int noy) {
    int dx = nox - grid->originX, dy = noy - grid->originY;
    if (dx == 0 && dy == 0) return; // nothing scrolled this frame
    if (grid->cave.seed != s_permSeed) PerlinInit(grid->cave.seed);

    int w = grid->width, h = grid->height;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int di = y * w + x;
            int sx = x + dx, sy = y + dy; // where this new cell came from
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                int si = sy * w + sx;       // shift existing (dynamic) cell over
                grid->sCells[di] = grid->cells[si];
                grid->sFlow[di]  = grid->flow[si];
                grid->sLife[di]  = grid->life[si];
            } else {                        // newly exposed edge: generate cave
                Cell c = TerrainAt(&grid->cave, nox + x, noy + y);
                grid->sCells[di] = c;
                grid->sFlow[di]  = 0;
                grid->sLife[di]  = MATERIALS[c].life;
            }
        }
    }
    size_t count = (size_t)w * h;
    memcpy(grid->cells, grid->sCells, count * sizeof(Cell));
    memcpy(grid->flow,  grid->sFlow,  count * sizeof(int8_t));
    memcpy(grid->life,  grid->sLife,  count * sizeof(uint8_t));
    grid->originX = nox;
    grid->originY = noy;
}
