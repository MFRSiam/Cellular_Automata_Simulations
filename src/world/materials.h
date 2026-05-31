//
// materials.h - the catalogue of cell materials and their static properties.
//
#ifndef MATERIALS_H
#define MATERIALS_H

#include "core.h"

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
    CELL_VAPOR,
    CELL_GAS,
    CELL_LAVA,
    CELL_ROCK,
    CELL_MUD,
    CELL_SANDSTONE, // sandy biome wall
    CELL_ICE,       // cold biome wall (melts near heat)
    CELL_MOSS,      // jungle biome wall (flammable)
    CELL_GOLD,      // rare glowing ore embedded in walls
    CELL_GLASS,     // acid-proof solid (contains acid)
    CELL_METAL,     // hardy: acid-proof, doesn't melt in lava
    CELL_OBSIDIAN,  // cooled lava; a dark glass (acid-proof)
    CELL_BASALT,    // black stone formed when water quenches lava
    CELL_MOLTEN_GLASS, // hot liquid (sand + lava); cools into glass
    CELL_COUNT,
} Cell;

typedef enum CellType {
    TYPE_EMPTY,
    TYPE_SOLID,   // never moves
    TYPE_POWDER,  // falls and piles
    TYPE_LIQUID,  // falls and flows
    TYPE_GAS,     // rises and diffuses
} CellType;

typedef struct MatInfo {
    const char *name;
    Color   color;
    CellType type;
    int     density;    // heavier sinks below lighter; air = 0, gases < 0
    bool    flammable;
    uint8_t life;       // lifetime for transient cells (0 = permanent)
    bool    emissive;   // glows (bloom); non-emissive is kept below the threshold
    bool    acidProof;  // acid cannot dissolve it (glass / metal / obsidian)
} MatInfo;

// The catalogue, indexed by Cell. Defined in materials.c.
extern const MatInfo MATERIALS[CELL_COUNT];

const char *CellName(Cell mat);

#endif // MATERIALS_H
