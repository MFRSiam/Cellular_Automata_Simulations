//
// hud.h - in-game overlay and pause menu, drawn with a custom font.
//
#ifndef HUD_H
#define HUD_H

#include "config.h"
#include "grid.h"

typedef enum UIAction {
    UI_NONE,
    UI_RESUME,
    UI_NEW_SEED,
    UI_CLEAR,
    UI_MENU,    // back to the main menu
} UIAction;

typedef enum MenuAction {
    MENU_NONE,
    MENU_NEW_GAME,
    MENU_STRUCTURES,
    MENU_SETTINGS,
    MENU_EXIT,
} MenuAction;

void Hud_Init(const AppConfig *cfg); // loads font (falls back to default)
void Hud_Free(void);

// Intro screen with New Game / Structures / Settings / Exit.
MenuAction Hud_DrawMenu(int screenW, int screenH);

// Settings screen: toggles render/biome options on `cfg`. Returns true on Back.
bool Hud_DrawSettings(int screenW, int screenH, AppConfig *cfg);

// Top overlay: selected material, biome, cave params, controls.
void Hud_DrawGame(const Grid *grid, Cell selected, const char *biomeName,
                  int brush, float zoom);

// Dimmed pause overlay with clickable buttons. Returns the chosen action.
UIAction Hud_DrawPause(int screenW, int screenH);

#endif // HUD_H
