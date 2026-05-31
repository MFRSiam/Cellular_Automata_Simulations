//
// hud.c - styled overlay + pause menu. Uses a TTF font from config if present,
// otherwise raylib's built-in font. All drawing goes through Text() so the
// font choice is centralized.
//
#include "hud.h"

#include <string.h>

static struct {
    Font  font;
    bool  custom;   // true if a real TTF was loaded
    int   size;
} H;

void Hud_Init(const AppConfig *cfg) {
    H.size = cfg->fontSize;
    const char *path = AssetPath(cfg->fontPath);
    if (FileExists(path)) {
        H.font = LoadFontEx(path, cfg->fontSize, 0, 250);
        H.custom = (H.font.texture.id != 0);
    }
    if (!H.custom) {
        H.font = GetFontDefault();
        TraceLog(LOG_INFO, "HUD: using built-in font (no '%s')", cfg->fontPath);
    }
    SetTextureFilter(H.font.texture, TEXTURE_FILTER_BILINEAR);
}

void Hud_Free(void) {
    if (H.custom) UnloadFont(H.font);
    memset(&H, 0, sizeof H);
}

// Centralized text helper with a soft drop shadow for legibility.
static void Text(const char *s, float x, float y, float size, Color col) {
    DrawTextEx(H.font, s, (Vector2){x + 1, y + 1}, size, 1.0f, (Color){0, 0, 0, 160});
    DrawTextEx(H.font, s, (Vector2){x, y}, size, 1.0f, col);
}
static float TextW(const char *s, float size) {
    return MeasureTextEx(H.font, s, size, 1.0f).x;
}

static void Panel(float x, float y, float w, float h) {
    DrawRectangleRounded((Rectangle){x, y, w, h}, 0.12f, 8, (Color){16, 18, 24, 180});
    DrawRectangleRoundedLines((Rectangle){x, y, w, h}, 0.12f, 8, (Color){90, 110, 140, 120});
}

void Hud_DrawGame(const Grid *grid, Cell selected, const char *biomeName,
                  int brush, float zoom) {
    Panel(8, 8, 470, 86);
    Text(TextFormat("%s", CellName(selected)), 18, 14, (float)H.size + 4, (Color){235, 240, 255, 255});
    Text(TextFormat("Biome: %s   Zoom: %.2f   Brush: %d", biomeName, zoom, brush),
         18, 40, 18, (Color){150, 200, 160, 255});
    Text(TextFormat("scale %.4f  thresh %.2f  oct %d  seed %u",
                    grid->cave.scale, grid->cave.threshold, grid->cave.octaves, grid->cave.seed),
         18, 62, 18, (Color){150, 170, 210, 255});

    const char *help =
        "1-9 mats  L lava R rock M mud V glass B metal N obsidian 0 erase   "
        "WASD pan  Q/E zoom  wheel brush   [ ] scale  , . thresh   C clear   ESC menu";
    int sw = GetScreenWidth();
    Panel(8, GetScreenHeight() - 34.0f, sw - 16.0f, 28);
    Text(help, 18, GetScreenHeight() - 30.0f, 16, (Color){190, 195, 205, 255});
}

// Draw a button; returns true if clicked this frame.
static bool Button(Rectangle r, const char *label) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color bg = hover ? (Color){70, 90, 130, 235} : (Color){40, 48, 64, 220};
    DrawRectangleRounded(r, 0.25f, 8, bg);
    DrawRectangleRoundedLines(r, 0.25f, 8, (Color){120, 150, 190, 200});
    float fs = (float)H.size;
    Text(label, r.x + (r.width - TextW(label, fs)) / 2, r.y + (r.height - fs) / 2, fs,
         RAYWHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

UIAction Hud_DrawPause(int screenW, int screenH) {
    DrawRectangle(0, 0, screenW, screenH, (Color){0, 0, 0, 140});

    float pw = 360, ph = 360;
    float px = (screenW - pw) / 2, py = (screenH - ph) / 2;
    Panel(px, py, pw, ph);

    const char *title = "PAUSED";
    Text(title, px + (pw - TextW(title, 40)) / 2, py + 24, 40, (Color){235, 240, 255, 255});

    float bx = px + 40, bw = pw - 80, bh = 52, gap = 16;
    float by = py + 96;
    UIAction act = UI_NONE;
    if (Button((Rectangle){bx, by + 0 * (bh + gap), bw, bh}, "Resume"))     act = UI_RESUME;
    if (Button((Rectangle){bx, by + 1 * (bh + gap), bw, bh}, "New Seed"))   act = UI_NEW_SEED;
    if (Button((Rectangle){bx, by + 2 * (bh + gap), bw, bh}, "Clear"))      act = UI_CLEAR;
    if (Button((Rectangle){bx, by + 3 * (bh + gap), bw, bh}, "Main Menu"))  act = UI_MENU;
    return act;
}

// Shared backdrop for the full-screen menu/settings scenes.
static void SceneBackdrop(int w, int h, const char *title) {
    DrawRectangleGradientV(0, 0, w, h, (Color){20, 24, 34, 255}, (Color){8, 9, 14, 255});
    Text(title, (w - TextW(title, 64)) / 2.0f, h * 0.18f, 64, (Color){235, 240, 255, 255});
    const char *sub = "Cellular Automata Sandbox";
    Text(sub, (w - TextW(sub, 22)) / 2.0f, h * 0.18f + 70, 22, (Color){120, 150, 190, 255});
}

MenuAction Hud_DrawMenu(int w, int h) {
    SceneBackdrop(w, h, "CAVE SIM");
    float bw = 320, bh = 56, gap = 16, bx = (w - bw) / 2.0f, by = h * 0.44f;
    MenuAction a = MENU_NONE;
    if (Button((Rectangle){bx, by + 0 * (bh + gap), bw, bh}, "New Game"))   a = MENU_NEW_GAME;
    if (Button((Rectangle){bx, by + 1 * (bh + gap), bw, bh}, "Structures")) a = MENU_STRUCTURES;
    if (Button((Rectangle){bx, by + 2 * (bh + gap), bw, bh}, "Settings"))   a = MENU_SETTINGS;
    if (Button((Rectangle){bx, by + 3 * (bh + gap), bw, bh}, "Exit"))       a = MENU_EXIT;
    return a;
}

bool Hud_DrawSettings(int w, int h, AppConfig *cfg) {
    SceneBackdrop(w, h, "SETTINGS");
    float bw = 360, bh = 54, gap = 16, bx = (w - bw) / 2.0f, by = h * 0.40f;
    int row = 0;
    if (Button((Rectangle){bx, by + row++ * (bh + gap), bw, bh},
               TextFormat("Bloom: %s",  cfg->bloom  ? "ON" : "OFF"))) cfg->bloom  = !cfg->bloom;
    if (Button((Rectangle){bx, by + row++ * (bh + gap), bw, bh},
               TextFormat("Water FX: %s", cfg->water ? "ON" : "OFF"))) cfg->water  = !cfg->water;
    if (Button((Rectangle){bx, by + row++ * (bh + gap), bw, bh},
               TextFormat("Heat FX: %s", cfg->heat   ? "ON" : "OFF"))) cfg->heat   = !cfg->heat;
    if (Button((Rectangle){bx, by + row++ * (bh + gap), bw, bh},
               TextFormat("Biomes: %s",  cfg->biomes ? "ON" : "OFF"))) cfg->biomes = !cfg->biomes;
    return Button((Rectangle){bx, by + (row + 0.5f) * (bh + gap), bw, bh}, "Back");
}
