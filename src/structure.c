//
// structure.c - registry, save/load, and deterministic placement.
//
#include "structure.h"
#include "core.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// World is divided into REGION x REGION cells; at most one structure per
// region, chosen by a hash so placement is stable across regenerations.
#define REGION 110
#define STRUCT_CHANCE 16 // out of 100 regions

static Structure s_list[MAX_STRUCTURES];
static int s_count = 0;

int Structure_Count(void) { return s_count; }

// --- save / load -----------------------------------------------------------
// Text format:  "w h\n" then h rows of w space-separated cell ids.
static bool ParseStructure(const char *text, Structure *out) {
    int w = 0, h = 0, pos = 0;
    if (sscanf(text, "%d %d%n", &w, &h, &pos) != 2) return false;
    if (w <= 0 || h <= 0 || w > REGION || h > REGION) return false;
    Cell *cells = calloc((size_t)w * h, sizeof(Cell));
    const char *p = text + pos;
    for (int i = 0; i < w * h; i++) {
        int v = 0, n = 0;
        if (sscanf(p, "%d%n", &v, &n) != 1) { free(cells); return false; }
        cells[i] = (v >= 0 && v < CELL_COUNT) ? (Cell)v : CELL_EMPTY;
        p += n;
    }
    *out = (Structure){.w = w, .h = h, .cells = cells};
    return true;
}

void Structure_LoadAll(const char *dir) {
    if (!DirectoryExists(dir)) return;
    FilePathList files = LoadDirectoryFilesEx(dir, ".txt", false);
    for (unsigned i = 0; i < files.count && s_count < MAX_STRUCTURES; i++) {
        char *text = LoadFileText(files.paths[i]);
        if (!text) continue;
        if (ParseStructure(text, &s_list[s_count])) {
            TraceLog(LOG_INFO, "STRUCT: loaded %s (%dx%d)", GetFileName(files.paths[i]),
                     s_list[s_count].w, s_list[s_count].h);
            s_count++;
        }
        UnloadFileText(text);
    }
    UnloadDirectoryFiles(files);
}

bool Structure_AddAndSave(int w, int h, const Cell *cells, const char *path) {
    if (w <= 0 || h <= 0 || w > REGION || h > REGION || s_count >= MAX_STRUCTURES) return false;

    // Build the text representation.
    size_t cap = 32 + (size_t)w * h * 4;
    char *text = malloc(cap);
    int len = snprintf(text, cap, "%d %d\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            len += snprintf(text + len, cap - len, "%d ", (int)cells[y * w + x]);
        len += snprintf(text + len, cap - len, "\n");
    }
    bool ok = SaveFileText(path, text);
    free(text);
    if (!ok) return false;

    // Add a live copy to the registry.
    Cell *copy = malloc((size_t)w * h * sizeof(Cell));
    memcpy(copy, cells, (size_t)w * h * sizeof(Cell));
    s_list[s_count++] = (Structure){.w = w, .h = h, .cells = copy};
    TraceLog(LOG_INFO, "STRUCT: saved %s (%dx%d)", path, w, h);
    return true;
}

// --- deterministic placement ----------------------------------------------
static int FloorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

static unsigned int Hash(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

Cell StructureSampleAt(unsigned int seed, int wx, int wy) {
    if (s_count == 0) return CELL_EMPTY;

    int rx = FloorDiv(wx, REGION), ry = FloorDiv(wy, REGION);
    unsigned int h = Hash(rx, ry, seed);
    if (h % 100u >= (unsigned)STRUCT_CHANCE) return CELL_EMPTY;

    const Structure *st = &s_list[(h >> 8) % (unsigned)s_count];
    // Offset within the region, kept fully inside it (no cross-region spill).
    int spanX = REGION - st->w, spanY = REGION - st->h;
    int ox = rx * REGION + (spanX > 0 ? (int)((h >> 16) % (unsigned)spanX) : 0);
    int oy = ry * REGION + (spanY > 0 ? (int)((h >> 22) % (unsigned)spanY) : 0);

    if (wx < ox || wx >= ox + st->w || wy < oy || wy >= oy + st->h) return CELL_EMPTY;
    return st->cells[(wy - oy) * st->w + (wx - ox)];
}

void Structure_FreeAll(void) {
    for (int i = 0; i < s_count; i++) free(s_list[i].cells);
    s_count = 0;
}
