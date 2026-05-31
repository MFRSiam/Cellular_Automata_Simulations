//
// core.h - shared includes and global constants.
//
#ifndef CORE_H
#define CORE_H

#include <raylib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Size of one simulation cell in pixels. Grid resolution = pixels / CELL_SIZE.
#define CELL_SIZE 8

// Maximum simultaneous ripple effects.
#define MAX_RIPPLES 256

// Build a path next to the executable (raylib returns a trailing slash).
static inline const char *AssetPath(const char *rel) {
    return TextFormat("%s%s", GetApplicationDirectory(), rel);
}

#endif // CORE_H
