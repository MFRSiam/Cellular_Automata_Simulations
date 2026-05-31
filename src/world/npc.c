//
// npc.c - frogs, flies & worms (see npc.h).
//
#include "npc.h"
#include "materials.h"
#include "biome.h"
#include "core.h"

#include <math.h>
#include <stdlib.h>

#define MAX_NPCS  256
#define MAX_FROGS  30
#define MAX_FLIES  60
#define MAX_WORMS  20

typedef enum NpcKind { NPC_NONE, NPC_FROG, NPC_FLY, NPC_WORM } NpcKind;

typedef struct Npc {
    NpcKind kind;
    float   x, y;      // absolute world-cell coordinates (fractional)
    float   vx, vy;    // velocity in cells/second
    int     facing;    // -1 left, +1 right
    float   t;         // AI timer
    float   phase;     // animation phase
    bool    onGround;
    // worm-only: the rock cell it is currently chewing through
    int     tx, ty;
    float   chew;      // 0..CHEW_TIME progress
    bool    chewing;
} Npc;

#define WORM_CHEW_TIME 1.3f // seconds to gnaw one rock into mud

static Npc   s_npc[MAX_NPCS];
static float s_spawnT = 0.0f;
static float s_breedT = 0.0f;

static float Frand(void)              { return (float)GetRandomValue(0, 10000) / 10000.0f; }
static float Frange(float a, float b) { return a + (b - a) * Frand(); }

void Npc_Reset(void) {
    for (int i = 0; i < MAX_NPCS; i++) s_npc[i].kind = NPC_NONE;
    s_spawnT = 0.0f; s_breedT = 0.0f;
}

// --- grid queries (world coordinates) --------------------------------------
static Cell CellW(Grid *g, int wx, int wy) {
    int bx = wx - g->originX, by = wy - g->originY;
    if (!GridInBounds(g, bx, by)) return CELL_ROCK; // off-window reads as solid
    return GridGet(g, bx, by);
}
static void SetW(Grid *g, int wx, int wy, Cell c) {
    int bx = wx - g->originX, by = wy - g->originY;
    if (GridInBounds(g, bx, by)) GridSet(g, bx, by, c);
}
static bool SolidW(Grid *g, int wx, int wy) {
    CellType t = MATERIALS[CellW(g, wx, wy)].type;
    return t == TYPE_SOLID || t == TYPE_POWDER;
}
static bool DeadlyW(Grid *g, int wx, int wy) {
    Cell c = CellW(g, wx, wy);
    return c == CELL_FIRE || c == CELL_LAVA || c == CELL_ACID || c == CELL_ACID_GAS ||
           c == CELL_SPARK || c == CELL_MOLTEN_GLASS || c == CELL_MOLTEN_WAX;
}
static bool Diggable(Cell c) {
    return c == CELL_ROCK || c == CELL_SANDSTONE || c == CELL_COAL || c == CELL_BASALT;
}

static void SpawnBlood(Grid *g, float fx, float fy) {
    int cx = (int)floorf(fx), cy = (int)floorf(fy);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (GetRandomValue(0, 100) < 70 && CellW(g, cx + dx, cy + dy) == CELL_EMPTY)
                SetW(g, cx + dx, cy + dy, CELL_BLOOD);
}

static int Count(NpcKind k) {
    int n = 0;
    for (int i = 0; i < MAX_NPCS; i++) if (s_npc[i].kind == k) n++;
    return n;
}
static int FreeSlot(void) {
    for (int i = 0; i < MAX_NPCS; i++) if (s_npc[i].kind == NPC_NONE) return i;
    return -1;
}
static bool InWindow(Grid *g, float x, float y) {
    int bx = (int)floorf(x) - g->originX, by = (int)floorf(y) - g->originY;
    return bx >= 2 && bx < g->width - 2 && by >= 2 && by < g->height - 2;
}
static bool JungleAt(Grid *g, int wx, int wy) {
    return !g->cave.biomes || BiomeAt(wx, wy) == BIOME_JUNGLE;
}
static bool RockyAt(Grid *g, int wx, int wy) {
    return !g->cave.biomes || BiomeAt(wx, wy) == BIOME_ROCKY;
}

// --- per-critter AI ---------------------------------------------------------
static void UpdateFly(Grid *g, Npc *f, float dt) {
    f->phase += dt * 22.0f;

    // Separation: flies dislike crowding, so they steer away from neighbours.
    // This stops them all pooling in a low spot of the cave.
    float sx = 0, sy = 0;
    for (int j = 0; j < MAX_NPCS; j++) {
        if (s_npc[j].kind != NPC_FLY || &s_npc[j] == f) continue;
        float dx = f->x - s_npc[j].x, dy = f->y - s_npc[j].y, d2 = dx * dx + dy * dy;
        if (d2 < 9.0f && d2 > 0.0001f) { sx += dx / d2; sy += dy / d2; }
    }
    f->vx += sx * 4.0f * dt;
    f->vy += sy * 4.0f * dt;

    f->t -= dt;
    if (f->t <= 0.0f) { f->t = Frange(0.3f, 0.9f); f->vx += Frange(-3, 3); f->vy += Frange(-3, 3); }

    f->vy -= 0.8f * dt;            // slight lift, so they don't sink into pits
    f->vx *= 0.96f; f->vy *= 0.96f; // drag
    float sp = sqrtf(f->vx * f->vx + f->vy * f->vy);
    if (sp > 7.0f) { f->vx *= 7.0f / sp; f->vy *= 7.0f / sp; }

    float nx = f->x + f->vx * dt, ny = f->y + f->vy * dt;
    if (SolidW(g, (int)floorf(nx), (int)floorf(f->y))) { f->vx = -f->vx * 0.5f; nx = f->x; }
    if (SolidW(g, (int)floorf(f->x), (int)floorf(ny))) { f->vy = -f->vy * 0.5f; ny = f->y; }
    f->x = nx; f->y = ny;
}

static void UpdateFrog(Grid *g, Npc *fr, float dt) {
    fr->phase += dt * 6.0f;
    fr->vy += 28.0f * dt; // gravity (lower than before => higher, floatier hops)

    float nx = fr->x + fr->vx * dt;
    if (SolidW(g, (int)floorf(nx), (int)floorf(fr->y))) { fr->vx = 0; nx = fr->x; }
    fr->x = nx;

    float ny = fr->y + fr->vy * dt;
    fr->onGround = false;
    if (fr->vy > 0 && SolidW(g, (int)floorf(fr->x), (int)floorf(ny) + 1)) {
        ny = floorf(ny); fr->vy = 0; fr->onGround = true;
    } else if (fr->vy < 0 && SolidW(g, (int)floorf(fr->x), (int)floorf(ny))) {
        fr->vy = 0; ny = fr->y;
    }
    fr->y = ny;

    if (fr->onGround) {
        fr->vx *= 0.7f;
        fr->t -= dt;
        if (fr->t <= 0.0f) {
            // Actively hunt: find the nearest fly in a wide radius and leap at it.
            float best = 18.0f, tx = 0, ty = 0; bool found = false;
            for (int i = 0; i < MAX_NPCS; i++) {
                if (s_npc[i].kind != NPC_FLY) continue;
                float d = fabsf(s_npc[i].x - fr->x) + fabsf(s_npc[i].y - fr->y);
                if (d < best) { best = d; tx = s_npc[i].x; ty = s_npc[i].y; found = true; }
            }
            int dir; float jump;
            if (found) {
                dir = (tx > fr->x) ? 1 : -1;
                fr->vx = dir * Frange(6.0f, 9.0f);
                jump = Frange(15.0f, 20.0f);
                if (ty < fr->y - 2) jump += Frange(3.0f, 7.0f); // fly is up high -> leap higher
                fr->t = Frange(0.35f, 0.8f);                    // chase quickly
            } else {
                dir = GetRandomValue(0, 1) ? 1 : -1;
                fr->vx = dir * Frange(3.0f, 6.0f);
                jump = Frange(12.0f, 17.0f);
                fr->t = Frange(0.9f, 2.0f);
            }
            fr->facing = dir;
            fr->vy = -jump;
        }
    }

    // Catch flies within reach.
    for (int i = 0; i < MAX_NPCS; i++)
        if (s_npc[i].kind == NPC_FLY &&
            fabsf(s_npc[i].x - fr->x) < 1.5f && fabsf(s_npc[i].y - fr->y) < 1.5f)
            s_npc[i].kind = NPC_NONE;
}

// Worms burrow through rock, slowly gnawing each cell into mud over time.
static void UpdateWorm(Grid *g, Npc *w, float dt) {
    w->phase += dt * 4.0f;
    int cx = (int)floorf(w->x), cy = (int)floorf(w->y);
    if (w->facing == 0) w->facing = GetRandomValue(0, 1) ? 1 : -1;

    // Already chewing a rock? Keep at it until it crumbles into mud.
    if (w->chewing) {
        if (Diggable(CellW(g, w->tx, w->ty)) && abs(w->tx - cx) <= 1 && abs(w->ty - cy) <= 1) {
            w->chew += dt;
            if (w->chew >= WORM_CHEW_TIME) {
                SetW(g, w->tx, w->ty, CELL_MUD);          // rock fully eaten -> mud
                w->x = w->tx + 0.5f; w->y = w->ty + 0.5f; // crawl into it
                if (w->tx != cx) w->facing = (w->tx > cx) ? 1 : -1;
                w->chewing = false; w->chew = 0.0f;
            }
            return;
        }
        w->chewing = false; w->chew = 0.0f; // target vanished
    }

    // Move on a slow timer when not chewing.
    w->t -= dt;
    if (w->t > 0.0f) return;
    w->t = Frange(0.12f, 0.22f);
    if (GetRandomValue(0, 100) < 8) w->facing = -w->facing;
    int order[4][2] = {{w->facing, 0}, {0, 1}, {-w->facing, 0}, {0, -1}};

    // Start chewing the first rock we can reach.
    for (int k = 0; k < 4; k++) {
        int tx = cx + order[k][0], ty = cy + order[k][1];
        if (Diggable(CellW(g, tx, ty))) { w->tx = tx; w->ty = ty; w->chewing = true; w->chew = 0.0f; return; }
    }
    // No rock: drop if air below, else crawl into an empty cell.
    if (CellW(g, cx, cy + 1) == CELL_EMPTY) { w->y += 1.0f; return; }
    for (int k = 0; k < 4; k++) {
        int tx = cx + order[k][0], ty = cy + order[k][1];
        if (CellW(g, tx, ty) == CELL_EMPTY) {
            w->x = tx + 0.5f; w->y = ty + 0.5f;
            if (order[k][0]) w->facing = order[k][0];
            return;
        }
    }
}

// Flies multiply: every so often one buds a new fly into an adjacent cell.
static void BreedFlies(Grid *g) {
    if (Count(NPC_FLY) >= MAX_FLIES) return;
    int idx = -1;
    for (int a = 0; a < 8; a++) {
        int i = GetRandomValue(0, MAX_NPCS - 1);
        if (s_npc[i].kind == NPC_FLY) { idx = i; break; }
    }
    if (idx < 0) return;
    int cx = (int)floorf(s_npc[idx].x), cy = (int)floorf(s_npc[idx].y);
    static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int k = 0; k < 4; k++) {
        int ex = cx + nb[k][0], ey = cy + nb[k][1];
        if (CellW(g, ex, ey) == CELL_EMPTY) {
            int s = FreeSlot(); if (s < 0) return;
            s_npc[s] = (Npc){ .kind = NPC_FLY, .x = ex + 0.5f, .y = ey + 0.5f, .t = Frange(0.2f, 0.8f) };
            return;
        }
    }
}

static void TrySpawn(Grid *g) {
    // Flies in jungle air.
    if (Count(NPC_FLY) < MAX_FLIES) {
        for (int a = 0; a < 6; a++) {
            int bx = GetRandomValue(4, g->width - 5), by = GetRandomValue(4, g->height - 5);
            int wx = g->originX + bx, wy = g->originY + by;
            if (GridGet(g, bx, by) == CELL_EMPTY && JungleAt(g, wx, wy)) {
                int s = FreeSlot(); if (s < 0) break;
                s_npc[s] = (Npc){ .kind = NPC_FLY, .x = wx + 0.5f, .y = wy + 0.5f, .t = Frange(0.2f, 0.8f) };
                break;
            }
        }
    }
    // Carrion: flies gather around fresh blood (anywhere, not only jungle).
    if (Count(NPC_FLY) < MAX_FLIES) {
        for (int a = 0; a < 5; a++) {
            int bx = GetRandomValue(4, g->width - 5), by = GetRandomValue(4, g->height - 5);
            if (GridGet(g, bx, by) != CELL_BLOOD) continue;
            static const int nb[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            for (int k = 0; k < 4; k++) {
                int ex = bx + nb[k][0], ey = by + nb[k][1];
                if (GridInBounds(g, ex, ey) && GridGet(g, ex, ey) == CELL_EMPTY) {
                    int s = FreeSlot(); if (s < 0) break;
                    s_npc[s] = (Npc){ .kind = NPC_FLY, .x = (g->originX + ex) + 0.5f,
                                      .y = (g->originY + ey) + 0.5f, .t = Frange(0.2f, 0.8f) };
                    break;
                }
            }
            break;
        }
    }
    // Frogs on jungle floors (drop a probe to find ground under an air pocket).
    if (Count(NPC_FROG) < MAX_FROGS) {
        for (int a = 0; a < 12; a++) {
            int bx = GetRandomValue(4, g->width - 5), by = GetRandomValue(4, g->height - 45);
            int wx = g->originX + bx;
            if (!JungleAt(g, wx, g->originY + by) || GridGet(g, bx, by) != CELL_EMPTY) continue;
            int fy = -1;
            for (int d = 1; d <= 40 && by + d < g->height - 1; d++) {
                Cell below = GridGet(g, bx, by + d);
                if (below == CELL_EMPTY) continue;
                CellType t = MATERIALS[below].type;
                if (t == TYPE_SOLID || t == TYPE_POWDER) fy = by + d - 1;
                break;
            }
            if (fy < 0 || GridGet(g, bx, fy) != CELL_EMPTY) continue;
            int s = FreeSlot(); if (s < 0) break;
            s_npc[s] = (Npc){ .kind = NPC_FROG, .x = wx + 0.5f, .y = (float)(g->originY + fy),
                              .facing = 1, .t = Frange(0.3f, 1.2f), .onGround = true };
            break;
        }
    }
    // Worms inside rocky-biome rock.
    if (Count(NPC_WORM) < MAX_WORMS) {
        for (int a = 0; a < 10; a++) {
            int bx = GetRandomValue(4, g->width - 5), by = GetRandomValue(4, g->height - 5);
            int wx = g->originX + bx, wy = g->originY + by;
            if (RockyAt(g, wx, wy) && Diggable(GridGet(g, bx, by))) {
                int s = FreeSlot(); if (s < 0) break;
                s_npc[s] = (Npc){ .kind = NPC_WORM, .x = wx + 0.5f, .y = wy + 0.5f,
                                  .facing = GetRandomValue(0, 1) ? 1 : -1, .t = Frange(0.1f, 0.3f) };
                break;
            }
        }
    }
}

void Npc_Update(Grid *g, float dt) {
    if (dt > 0.1f) dt = 0.1f;

    for (int i = 0; i < MAX_NPCS; i++) {
        Npc *n = &s_npc[i];
        if (n->kind == NPC_NONE) continue;
        if (DeadlyW(g, (int)floorf(n->x), (int)floorf(n->y))) { SpawnBlood(g, n->x, n->y); n->kind = NPC_NONE; continue; }
        if (!InWindow(g, n->x, n->y)) { n->kind = NPC_NONE; continue; }
        if (n->kind == NPC_FROG)      UpdateFrog(g, n, dt);
        else if (n->kind == NPC_FLY)  UpdateFly(g, n, dt);
        else                          UpdateWorm(g, n, dt);
    }

    s_spawnT -= dt;
    if (s_spawnT <= 0.0f) { s_spawnT = 0.4f; TrySpawn(g); }
    s_breedT -= dt;
    if (s_breedT <= 0.0f) { s_breedT = 1.0f; BreedFlies(g); }
}

// --- pixel-art drawing (world space, inside BeginMode2D) -------------------
static void Pixel(float x, float y, float ps, Color c) {
    DrawRectangle((int)floorf(x), (int)floorf(y), (int)ceilf(ps) + 1, (int)ceilf(ps) + 1, c);
}

static void DrawFrog(const Npc *fr) {
    static const char *F[6] = {
        ".E...E.",
        "EPGGGPE",
        "GGGGGGG",
        "GBBBBBG",
        "DGGGGGD",
        "D.....D",
    };
    const int w = 7, h = 6;
    float ps = CELL_SIZE * 0.5f;
    float cx = fr->x * CELL_SIZE, base = (fr->y + 1.0f) * CELL_SIZE;
    int f = fr->facing >= 0 ? 1 : -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            char ch = F[y][f >= 0 ? x : (w - 1 - x)];
            Color col; bool on = true;
            switch (ch) {
                case 'G': col = (Color){ 86, 166,  74, 255}; break;
                case 'B': col = (Color){158, 205, 128, 255}; break;
                case 'D': col = (Color){ 54, 120,  50, 255}; break;
                case 'E': col = (Color){245, 248, 238, 255}; break;
                case 'P': col = (Color){ 20,  28,  18, 255}; break;
                default:  on = false; break;
            }
            if (on) Pixel(cx - w * ps * 0.5f + x * ps, base - h * ps + y * ps, ps, col);
        }
}

static void DrawFly(const Npc *fl) {
    float ps = CELL_SIZE * 0.45f;
    float cx = fl->x * CELL_SIZE, cy = fl->y * CELL_SIZE;
    Color body = {32, 30, 38, 255};
    Color wing = {205, 215, 235, 190};
    bool up = sinf(fl->phase) >= 0.0f;
    // body (3 wide) + head
    for (int x = -1; x <= 1; x++) Pixel(cx + x * ps - ps * 0.5f, cy - ps * 0.5f, ps, body);
    Pixel(cx - ps * 0.5f, cy - ps * 1.5f, ps, body);
    // flapping wings
    float wy = up ? cy - ps * 1.6f : cy + ps * 0.5f;
    Pixel(cx - ps * 2.1f, wy, ps, wing);
    Pixel(cx + ps * 1.1f, wy, ps, wing);
}

static void DrawWorm(const Npc *w) {
    float ps = CELL_SIZE * 0.5f;
    float cx = w->x * CELL_SIZE, cy = w->y * CELL_SIZE;
    int f = w->facing >= 0 ? 1 : -1;
    Color body = {196, 122, 124, 255}, head = {150, 84, 86, 255};
    for (int s = 0; s < 4; s++) {
        float sx = cx - f * s * ps;
        float sy = cy + sinf(w->phase + s * 0.9f) * ps * 0.4f;
        Pixel(sx - ps * 0.5f, sy - ps * 0.5f, ps, s == 0 ? head : body);
    }
}

void Npc_Draw(void) {
    for (int i = 0; i < MAX_NPCS; i++) {
        switch (s_npc[i].kind) {
            case NPC_FROG: DrawFrog(&s_npc[i]); break;
            case NPC_FLY:  DrawFly(&s_npc[i]);  break;
            case NPC_WORM: DrawWorm(&s_npc[i]); break;
            default: break;
        }
    }
}
