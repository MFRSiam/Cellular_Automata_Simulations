//
// grid.h - the cellular-automata world: a window onto an infinite cave.
//
#ifndef GRID_H
#define GRID_H

#include "materials.h"

#define WATER_DISPERSION 6

// Simulation tiles: the buffer is divided into SIM_TILE x SIM_TILE tiles and
// fully-settled tiles are skipped by GridUpdate ("sleeping"). Any cell change
// wakes the 3x3 tile neighbourhood; a small random set of tiles is woken each
// frame so ambient life (grass, moss, coral...) still ticks over.
#define SIM_TILE 32

typedef struct Ripple {
    float x, y;      // center, in WORLD pixels
    float radius;
    float life;      // 0..1, also drives fade
    bool  active;
} Ripple;

typedef struct CaveParams {
    float scale;        // noise frequency per cell (zoom)
    float threshold;    // 0..1 cave openness
    float mudThreshold; // 0..1 vein density
    int   octaves;
    unsigned int seed;
    bool  biomes;       // apply biome variation
} CaveParams;

// A (width x height) window of cells; (originX,originY) is the world-cell
// coordinate of cell (0,0). Parallel arrays are row-major.
typedef struct Grid {
    int width, height;
    int originX, originY;
    Cell    *cells;
    int8_t  *flow;
    uint8_t *life;
    uint8_t *updated;
    Cell    *sCells;    // scratch for streaming
    int8_t  *sFlow;
    uint8_t *sLife;
    // Render snapshot: a consistent copy the renderer reads while the worker
    // thread keeps simulating into the live arrays (see GridSnapshot).
    Cell    *rCells;
    uint8_t *rLife;
    int      rOriginX, rOriginY;
    Ripple   rRipples[MAX_RIPPLES];
    // Sleeping-tile bookkeeping (see SIM_TILE).
    int      tilesX, tilesY;
    uint8_t *tileNow;     // tiles to process this step
    uint8_t *tileNext;    // tiles woken for the next step
    int      activeTiles; // last step's awake count (for the HUD)
    unsigned frame;       // per-grid step counter (drives scan direction)
    CaveParams cave;
    Ripple ripples[MAX_RIPPLES];
} Grid;

Grid GridCreate(int windowWidth, int windowHeight);
void GridFree(Grid *grid);
void GridClear(Grid *grid);

bool GridInBounds(const Grid *grid, int x, int y);
Cell GridGet(const Grid *grid, int x, int y);
void GridSet(Grid *grid, int x, int y, Cell mat);
void GridPaint(Grid *grid, int cx, int cy, int radius, Cell mat);

// Serial step (used by the editor; the game uses the split API below from a
// multithreaded worker pool).
void GridUpdate(Grid *grid);

// Split step for parallel execution: Prepare once, then UpdateStrip for
// disjoint column ranges [x0,x1) - ranges from different threads must be
// separated by at least one unprocessed strip wider than the max interaction
// distance (two-phase checkerboard) - then Finish once.
void GridUpdatePrepare(Grid *grid);
void GridUpdateStrip(Grid *grid, int x0, int x1);
void GridUpdateFinish(Grid *grid);

// Optional parallel-for hook, installed by sim.c when its worker pool exists.
// grid.c uses it to parallelise world generation. Calls fn(0..count-1, ud)
// from multiple threads and returns when all are done. NULL => run serially.
extern void (*GridParallelFor)(int count, void (*fn)(int index, void *ud), void *ud);

// Copy the live state into the render snapshot. Call under the sim lock; the
// drawing functions below then run without holding it.
void GridSnapshot(Grid *grid);
// Per-cell rectangle path (used by the editor's small canvas).
void GridDrawWorld(const Grid *grid, Camera2D camera);
// Fast path: fill `out` (width*height RGBA) from the snapshot; only the cells
// visible to `camera` (plus a margin) are recomputed, and far-zoom uses a flat
// colour fast path since cells are sub-pixel there anyway.
void GridFillPixels(const Grid *grid, Color *out, Camera2D camera);
// Draw the snapshot's water ripples (world space).
void GridDrawRipples(const Grid *grid);

// Rebuild the whole window from current cave params + origin.
void GridRegenerate(Grid *grid);
// Scroll the window to a new origin, streaming fresh cave into new edges.
void GridStreamTo(Grid *grid, int newOriginX, int newOriginY);

#endif // GRID_H
