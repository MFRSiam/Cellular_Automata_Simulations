//
// Cellular Automata: a falling-sand / fluid sandbox.
//
// The engine is data-driven: every material has a type (powder/liquid/gas/
// solid) plus a density and a couple of flags. Generic rules then handle
// gravity, buoyancy, fluid flow and diffusion, and a small set of reactions
// handles combustion, dissolving and phase changes.
//

#ifndef SANDSIMULATION_RAYLIB_APPLICATION_H
#define SANDSIMULATION_RAYLIB_APPLICATION_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

// Size of one cell in pixels. Grid resolution = window size / CELL_SIZE.
#define CELL_SIZE 8

// How far a liquid may look sideways in one step (controls flow / leveling).
#define WATER_DISPERSION 6

// Maximum number of ripple effects alive at once.
#define MAX_RIPPLES 256

// The materials a cell can hold. EMPTY must stay 0 so a zeroed grid is "air".
typedef enum Cell {
    CELL_EMPTY = 0,
    CELL_SAND,
    CELL_WATER,
    CELL_WOOD,
    CELL_OIL,
    CELL_ACID,
    CELL_SNOW,
    CELL_FIRE,
    CELL_SMOKE,
    CELL_VAPOR,   // steam: rises, condenses back to water
    CELL_GAS,     // flammable gas
    CELL_LAVA,    // hot liquid: ignites, cools to rock, boils water
    CELL_ROCK,    // solid cave wall
    CELL_MUD,     // solid cave wall (softer look)
    CELL_COUNT,
} Cell;

// How a material moves under the generic rules.
typedef enum CellType {
    TYPE_EMPTY,
    TYPE_SOLID,   // never moves (wood)
    TYPE_POWDER,  // falls, piles up (sand, snow)
    TYPE_LIQUID,  // falls and flows to level out (water, oil, acid)
    TYPE_GAS,     // rises and diffuses (fire, smoke, vapor, gas)
} CellType;

// Static description of a material. Indexed by Cell.
typedef struct MatInfo {
    const char *name;
    Color color;
    CellType type;
    int density;       // heavier sinks below lighter; air = 0, gases < 0
    bool flammable;    // can be ignited by fire
    uint8_t life;      // lifetime for transient cells (0 = permanent)
} MatInfo;

// An expanding ring drawn on the water surface (splashes, condensing vapor).
typedef struct Ripple {
    float x, y;      // center, in WORLD pixels
    float radius;    // current radius, in pixels
    float life;      // remaining lifetime, 0..1 (also drives fade)
    bool active;
} Ripple;

// Tunable parameters for the procedural Perlin cave. Change at runtime and
// call GridRegenerate to see them take effect.
typedef struct CaveParams {
    float scale;        // noise frequency per cell (higher = zoomed-in detail)
    float threshold;    // 0..1 cave openness (higher = more open cave)
    float mudThreshold; // 0..1 how much of the rock becomes mud veins
    int   octaves;      // fBm octaves (more = more detail)
    unsigned int seed;  // world seed
} CaveParams;

// The simulation grid. It is a *window* of (width x height) cells onto an
// infinite world; (originX, originY) is the world-cell coordinate of the
// top-left cell. Parallel arrays are row-major: index = y * width + x.
typedef struct Grid {
    int width;          // number of cells across
    int height;         // number of cells down
    int originX, originY; // world-cell coordinate of cell (0,0)
    Cell *cells;        // material in each cell
    int8_t *flow;       // per-cell horizontal momentum (-1 / 0 / +1)
    uint8_t *life;      // per-cell remaining lifetime (for fire/smoke/vapor)
    uint8_t *updated;   // per-frame guard so a moved cell isn't stepped twice
    Cell *sCells;       // scratch buffers used while streaming/scrolling
    int8_t *sFlow;
    uint8_t *sLife;
    CaveParams cave;
    Ripple ripples[MAX_RIPPLES];
} Grid;

// Allocate a grid sized to fit the given window dimensions. Free with GridFree.
Grid GridCreate(int windowWidth, int windowHeight);
void GridFree(Grid *grid);
void GridClear(Grid *grid);

bool GridInBounds(const Grid *grid, int x, int y);
Cell GridGet(const Grid *grid, int x, int y);
void GridSet(Grid *grid, int x, int y, Cell mat); // also (re)initializes life

// Paint a filled circle of `mat` centered on (cx, cy) with the given radius.
void GridPaint(Grid *grid, int cx, int cy, int radius, Cell mat);

void GridUpdate(Grid *grid);

// Draw the grid through the given 2D camera, culling cells outside the view.
void GridDraw(const Grid *grid, Camera2D camera);

// Regenerate every cell in the window from the current cave params + origin.
// Call on startup and whenever cave params (scale/threshold/seed/...) change.
void GridRegenerate(Grid *grid);

// Scroll the window so its top-left is the given world-cell coordinate,
// streaming freshly generated cave into newly exposed edges. Cheap no-op if
// the origin is unchanged. This is what makes the world endless while panning.
void GridStreamTo(Grid *grid, int newOriginX, int newOriginY);

// Display name for a material (for the HUD).
const char *CellName(Cell mat);

#endif //SANDSIMULATION_RAYLIB_APPLICATION_H
