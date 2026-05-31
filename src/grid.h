//
// grid.h - the cellular-automata world: a window onto an infinite cave.
//
#ifndef GRID_H
#define GRID_H

#include "materials.h"

#define WATER_DISPERSION 6

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

void GridUpdate(Grid *grid);
void GridDrawWorld(const Grid *grid, Camera2D camera);

// Rebuild the whole window from current cave params + origin.
void GridRegenerate(Grid *grid);
// Scroll the window to a new origin, streaming fresh cave into new edges.
void GridStreamTo(Grid *grid, int newOriginX, int newOriginY);

#endif // GRID_H
