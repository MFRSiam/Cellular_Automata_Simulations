//
// biome.h - large-scale regions that change the look & terrain of the cave.
//
#ifndef BIOME_H
#define BIOME_H

#include "materials.h"

typedef enum Biome {
    BIOME_OPEN,    // big airy caverns
    BIOME_ROCKY,   // dense grey rock
    BIOME_SANDY,   // sandstone desert
    BIOME_JUNGLE,  // mossy, humid
    BIOME_COLD,    // icy
    BIOME_VOID,    // rare vast empty chasm
    BIOME_COUNT,
} Biome;

typedef struct BiomeInfo {
    const char *name;
    Cell  wallPrimary;    // bulk wall material
    Cell  wallSecondary;  // veins / accents
    float opennessBias;   // added to cave threshold (+ = more open)
    float scaleMul;       // multiplies cave noise frequency (look of the rock)
    int   octaveDelta;    // +/- octaves (roughness)
    Color skyTop;         // parallax background gradient
    Color skyBottom;
    Color hill;           // parallax silhouette tint
} BiomeInfo;

extern const BiomeInfo BIOMES[BIOME_COUNT];

// Which biome a world cell belongs to (uses low-frequency temp/humidity noise).
Biome BiomeAt(int worldX, int worldY);

#endif // BIOME_H
