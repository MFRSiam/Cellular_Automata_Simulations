//
// raygui_impl.c - the single translation unit that compiles raygui.
//
// raygui is header-only; exactly one .c file must define RAYGUI_IMPLEMENTATION.
// Everyone else just includes <raygui.h>. The header is provided on the include
// path by CMake (fetched alongside raylib).
//
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
