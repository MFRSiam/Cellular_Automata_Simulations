//
// materials.c - the material catalogue.
//
#include "materials.h"

const MatInfo MATERIALS[CELL_COUNT] = {
    //                    name          color                  type        dens  flam  life  emis  acidProof
    [CELL_EMPTY]        = {"Eraser",    {0, 0, 0, 0},         TYPE_EMPTY,    0, false,   0, false, false},
    [CELL_SAND]         = {"Sand",      {200, 168, 104, 255}, TYPE_POWDER, 200, false,   0, false, false},
    [CELL_WATER]        = {"Water",     { 64, 128, 220, 255}, TYPE_LIQUID, 100, false,   0, false, false},
    [CELL_WOOD]         = {"Wood",      {112,  74,  40, 255}, TYPE_SOLID,  500, true,    0, false, false},
    [CELL_OIL]          = {"Oil",       {120,  90,  40, 255}, TYPE_LIQUID,  60, true,    0, false, false},
    [CELL_ACID]         = {"Acid",      {120, 200,  60, 255}, TYPE_LIQUID, 120, false,   0, false, false},
    [CELL_SNOW]         = {"Snow",      {220, 226, 240, 255}, TYPE_POWDER,  40, false,   0, false, false},
    [CELL_FIRE]         = {"Fire",      {255, 140,  30, 255}, TYPE_GAS,    -15, false,  70, true,  false},
    [CELL_SMOKE]        = {"Smoke",     { 60,  60,  68, 255}, TYPE_GAS,    -25, false, 180, false, false},
    [CELL_VAPOR]        = {"Vapor",     {190, 200, 215, 255}, TYPE_GAS,    -10, false, 255, false, false},
    [CELL_GAS]          = {"Flammable Gas", {140, 190, 110, 160}, TYPE_GAS,  -5, true,   0, false, false},
    [CELL_INERT_GAS]    = {"Inert Gas", {170, 175, 195, 120}, TYPE_GAS,     -7, false,   0, false, true},
    [CELL_ACID_GAS]     = {"Acid Gas",  {160, 210,  90, 150}, TYPE_GAS,     -6, false,   0, false, false},
    [CELL_LAVA]         = {"Lava",      {255, 100,  20, 255}, TYPE_LIQUID, 250, false,   0, true,  false},
    [CELL_ROCK]         = {"Rock",      { 78,  80,  88, 255}, TYPE_SOLID,  900, false,   0, false, false},
    [CELL_MUD]          = {"Mud",       { 80,  60,  42, 255}, TYPE_SOLID,  800, false,   0, false, false},
    [CELL_SANDSTONE]    = {"Sandstone", {178, 146,  88, 255}, TYPE_SOLID,  820, false,   0, false, false},
    [CELL_ICE]          = {"Ice",       {150, 188, 212, 255}, TYPE_SOLID,  840, false,   0, false, false},
    [CELL_MOSS]         = {"Moss",      { 64, 110,  52, 255}, TYPE_SOLID,  780, true,    0, false, false},
    [CELL_GRASS]        = {"Grass",     { 86, 168,  66, 255}, TYPE_SOLID,  300, true,    0, false, false},
    [CELL_VINE]         = {"Vine",      { 60, 132,  58, 255}, TYPE_SOLID,  320, true,    0, false, false},
    [CELL_GOLD]         = {"Gold",      {255, 210,  70, 255}, TYPE_POWDER,1900, false,   0, true,  true},
    [CELL_COPPER]       = {"Copper",    {205, 127,  77, 255}, TYPE_SOLID, 1000, false,   0, false, true},
    [CELL_CRYSTAL]      = {"Crystal",   {150, 220, 255, 255}, TYPE_SOLID,  900, false,   0, true,  true},
    [CELL_GLASS]        = {"Glass",     {150, 180, 195, 150}, TYPE_SOLID,  860, false,   0, false, true},
    [CELL_METAL]        = {"Metal",     {120, 126, 138, 255}, TYPE_SOLID, 1200, false,   0, false, true},
    [CELL_OBSIDIAN]     = {"Obsidian",  { 44,  34,  58, 255}, TYPE_SOLID,  880, false,   0, false, true},
    [CELL_BASALT]       = {"Basalt",    { 34,  34,  40, 255}, TYPE_SOLID,  870, false,   0, false, false},
    [CELL_MOLTEN_GLASS] = {"Molten Glass", {255, 170, 70, 255}, TYPE_LIQUID, 240, false, 0, true, false},
    [CELL_SALT]         = {"Salt",      {235, 238, 245, 255}, TYPE_POWDER, 220, false,   0, false, false},
    [CELL_ASH]          = {"Ash",       {120, 118, 116, 255}, TYPE_POWDER, 150, false,   0, false, false},
    [CELL_COAL]         = {"Coal",      { 40,  40,  46, 255}, TYPE_SOLID,  700, true,    0, false, false},
    [CELL_GUNPOWDER]    = {"Gunpowder", { 72,  72,  80, 255}, TYPE_POWDER, 250, false,   0, false, false},
    [CELL_SPARK]        = {"Spark",     {200, 230, 255, 255}, TYPE_GAS,    -30, false,   8, true,  false},
    [CELL_MERCURY]      = {"Mercury",   {190, 196, 206, 255}, TYPE_LIQUID,1800, false,   0, false, true},
    [CELL_WAX]          = {"Wax",       {230, 220, 170, 255}, TYPE_SOLID,  400, false,   0, false, false},
    [CELL_MOLTEN_WAX]   = {"Molten Wax",{240, 200, 120, 255}, TYPE_LIQUID, 230, false,   0, true,  false},
    [CELL_BLOOD]        = {"Blood",     {150,  24,  28, 255}, TYPE_LIQUID, 110, false,   0, false, false},
    [CELL_CORAL]        = {"Coral",     {255, 110, 150, 255}, TYPE_SOLID,  820, false,   0, false, false},
};

const char *CellName(Cell mat) { return MATERIALS[mat].name; }
