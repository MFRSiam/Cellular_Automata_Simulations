//
// structure.h - hand-made structures placed deterministically in the world.
//
// A structure is a small grid of cells. They are placed by a pure function of
// world coordinates (StructureSampleAt), so they survive streaming and reappear
// in the same spots whenever the world is regenerated with the same seed.
//
// Dev workflow: select a region in-game, save it; it is added to the registry
// and written to structures/ so it loads next launch.
//
#ifndef STRUCTURE_H
#define STRUCTURE_H

#include "materials.h"

#define MAX_STRUCTURES 64

typedef struct Structure {
    int   w, h;
    Cell *cells; // w*h, row-major; CELL_EMPTY = transparent (keep terrain)
} Structure;

// Load every structures/*.txt into the registry. Safe if the dir is missing.
void Structure_LoadAll(const char *dir);
int  Structure_Count(void);

// Register a captured region and write it to `path`. Takes a copy of `cells`.
bool Structure_AddAndSave(int w, int h, const Cell *cells, const char *path);

// Material to place at an absolute world cell, or CELL_EMPTY if none.
Cell StructureSampleAt(unsigned int seed, int wx, int wy);

void Structure_FreeAll(void);

#endif // STRUCTURE_H
