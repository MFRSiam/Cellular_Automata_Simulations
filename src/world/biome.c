//
// biome.c - biome catalogue + classification from temperature/humidity noise.
//
#include "biome.h"
#include "noise.h"

// Openness biases are kept gentle (small magnitude) so neighbouring biomes
// don't produce a hard density "cliff" where they meet; the Void is the one
// deliberate exception (vast open chasms).
const BiomeInfo BIOMES[BIOME_COUNT] = {
    //                name      wallA          wallB         bias    scaleMul oct  skyTop                skyBottom             hill
    [BIOME_OPEN]   = {"Open",   CELL_ROCK,      CELL_MUD,     +0.06f, 0.80f, -1,  { 40,  46,  66, 255}, { 14,  16,  26, 255}, { 60,  66,  90, 255}},
    [BIOME_ROCKY]  = {"Rocky",  CELL_ROCK,      CELL_MUD,     -0.03f, 1.15f, +1,  { 38,  40,  48, 255}, { 12,  12,  16, 255}, { 64,  66,  76, 255}},
    [BIOME_SANDY]  = {"Sandy",  CELL_SAND,      CELL_SANDSTONE,-0.02f, 1.00f,  0,  { 92,  72,  44, 255}, { 30,  22,  14, 255}, {150, 116,  70, 255}},
    [BIOME_JUNGLE] = {"Jungle", CELL_MUD,       CELL_MOSS,    +0.03f, 1.05f, +1,  { 24,  52,  34, 255}, {  8,  18,  12, 255}, { 46,  86,  52, 255}},
    [BIOME_COLD]   = {"Cold",   CELL_ICE,       CELL_ROCK,    +0.02f, 0.95f,  0,  { 60,  78,  96, 255}, { 18,  24,  34, 255}, {120, 150, 178, 255}},
    [BIOME_VOID]   = {"Void",   CELL_OBSIDIAN,  CELL_COPPER,  +0.40f, 0.50f, -2,  {  8,   4,  16, 255}, {  2,   1,   6, 255}, { 40,  20,  60, 255}},
    [BIOME_CORAL]  = {"Coral",  CELL_CORAL,     CELL_SAND,    -0.10f, 0.70f,  0,  { 30, 120, 140, 255}, {  6,  34,  52, 255}, { 60, 180, 170, 255}},
};

Biome BiomeAt(int worldX, int worldY) {
    // Domain warp: nudge the sample point by mid-frequency noise so biome
    // borders are organic and wiggly instead of smooth blobs.
    float wfx = NoisePerlin2(worldX * 0.003f + 11.0f, worldY * 0.003f + 11.0f) * 90.0f;
    float wfy = NoisePerlin2(worldX * 0.003f + 71.0f, worldY * 0.003f + 71.0f) * 90.0f;
    float X = worldX + wfx, Y = worldY + wfy;

    // Rare vast voids: very low frequency, high cutoff.
    float v = NoiseFbm(X * 0.0006f + 777.0f, Y * 0.0006f + 333.0f, 2);
    if (v > 0.80f) return BIOME_VOID;

    // Two independent low-frequency fields.
    float temp  = NoiseFbm(X * 0.0010f + 1000.0f, Y * 0.0010f + 1000.0f, 2);
    float humid = NoiseFbm(X * 0.0010f + 5000.0f, Y * 0.0010f + 9000.0f, 2);

    if (temp < 0.35f)                  return BIOME_COLD;
    if (temp > 0.66f && humid < 0.40f) return BIOME_SANDY;
    if (temp > 0.60f && humid > 0.78f) return BIOME_CORAL;  // hot + very wet: reef
    if (temp > 0.55f && humid > 0.60f) return BIOME_JUNGLE;
    if (humid > 0.70f)                 return BIOME_OPEN;
    return BIOME_ROCKY;
}
