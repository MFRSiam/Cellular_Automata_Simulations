//
// hud.c - styled overlay + pause menu. Uses a TTF font from config if present,
// otherwise raylib's built-in font. All drawing goes through Text() so the
// font choice is centralized.
//
#include "hud.h"
#include "materials.h"

#include "raygui.h"

#include <string.h>
#include <math.h>

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
                  int brush, float zoom, int posX, int posY) {
    Panel(8, 8, 470, 108);
    Text(TextFormat("%s", CellName(selected)), 18, 14, (float)H.size + 4, (Color){235, 240, 255, 255});
    Text(TextFormat("Biome: %s   Zoom: %.2f   Brush: %d", biomeName, zoom, brush),
         18, 40, 18, (Color){150, 200, 160, 255});
    // Position relative to the world origin (0,0).
    int dist = (int)sqrtf((float)posX * posX + (float)posY * posY);
    Text(TextFormat("Pos: %d, %d   (%d from origin)", posX, posY, dist),
         18, 62, 18, (Color){210, 190, 150, 255});
    int totalTiles = grid->tilesX * grid->tilesY;
    Text(TextFormat("scale %.4f  thresh %.2f  oct %d  seed %u  sim %d%%",
                    grid->cave.scale, grid->cave.threshold, grid->cave.octaves, grid->cave.seed,
                    totalTiles ? grid->activeTiles * 100 / totalTiles : 0),
         18, 84, 18, (Color){150, 170, 210, 255});

    const char *help =
        "Pick material from the palette ->     WASD pan   Q/E zoom   wheel brush   "
        "[ ] scale   , . thresh   G seed   C clear   F2 capture   ESC menu";
    int sw = GetScreenWidth();
    Panel(8, GetScreenHeight() - 34.0f, sw - 16.0f, 28);
    Text(help, 18, GetScreenHeight() - 30.0f, 16, (Color){190, 195, 205, 255});
}

// --- material palette -------------------------------------------------------
// Materials offered for painting (CELL_EMPTY at the end acts as the eraser).
static const Cell PAL[] = {
    CELL_SAND, CELL_WATER, CELL_WOOD, CELL_OIL, CELL_ACID, CELL_SNOW,
    CELL_FIRE, CELL_GAS, CELL_INERT_GAS, CELL_ACID_GAS, CELL_VAPOR, CELL_LAVA,
    CELL_ROCK, CELL_MUD, CELL_SANDSTONE, CELL_ICE, CELL_MOSS, CELL_GRASS,
    CELL_VINE, CELL_GOLD, CELL_COPPER, CELL_CRYSTAL, CELL_CORAL, CELL_GLASS,
    CELL_METAL, CELL_OBSIDIAN, CELL_BASALT, CELL_SALT, CELL_ASH, CELL_COAL,
    CELL_GUNPOWDER, CELL_SPARK, CELL_MERCURY, CELL_WAX, CELL_BLOOD, CELL_EMPTY,
};
static const int PAL_COUNT = (int)(sizeof(PAL) / sizeof(PAL[0]));
#define PAL_COLS 4

const Cell *Hud_PaletteList(int *count) {
    *count = PAL_COUNT;
    return PAL;
}

Rectangle Hud_PaletteRect(int screenW, int screenH) {
    (void)screenH;
    const float pw = 184.0f, pad = 12, gap = 6;
    float sw = (pw - 2 * pad - (PAL_COLS - 1) * gap) / PAL_COLS;
    int rows = (PAL_COUNT + PAL_COLS - 1) / PAL_COLS;
    float ph = 34 + rows * (sw + gap) + 52;
    return (Rectangle){ screenW - pw - 10.0f, 44.0f, pw, ph };
}

void Hud_DrawPalette(Cell *selected, int *brush, int brushMax, int screenW, int screenH) {
    Rectangle P = Hud_PaletteRect(screenW, screenH);
    Panel(P.x, P.y, P.width, P.height);
    Text("MATERIALS", P.x + 12, P.y + 8, 18, (Color){235, 240, 255, 255});

    const float pad = 12, gap = 6;
    float sw = (P.width - 2 * pad - (PAL_COLS - 1) * gap) / PAL_COLS;
    float gx = P.x + pad, gy = P.y + 34;
    Vector2 m = GetMousePosition();

    for (int i = 0; i < PAL_COUNT; i++) {
        int cx = i % PAL_COLS, cy = i / PAL_COLS;
        Rectangle s = { gx + cx * (sw + gap), gy + cy * (sw + gap), sw, sw };
        Cell mat = PAL[i];
        bool hover = CheckCollisionPointRec(m, s);
        bool sel = (*selected == mat);

        DrawRectangleRounded(s, 0.22f, 4, (Color){10, 12, 16, 255});
        if (mat == CELL_EMPTY) {
            Text("ERA", s.x + sw * 0.5f - 13, s.y + sw * 0.5f - 8, 15, (Color){225, 130, 130, 255});
        } else {
            Color col = MATERIALS[mat].color; col.a = 255;
            DrawRectangleRounded((Rectangle){s.x + 3, s.y + 3, sw - 6, sw - 6}, 0.22f, 4, col);
        }
        Color border = sel ? (Color){255, 230, 120, 255}
                           : (hover ? (Color){185, 205, 235, 255} : (Color){70, 80, 100, 160});
        DrawRectangleRoundedLinesEx(s, 0.22f, 4, sel ? 2.5f : 1.5f, border);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) *selected = mat;
        if (hover) { // name tooltip to the left of the panel
            const char *nm = CellName(mat);
            float tw = TextW(nm, 16);
            DrawRectangleRounded((Rectangle){P.x - tw - 18, m.y - 14, tw + 12, 24}, 0.4f, 6, (Color){16, 18, 24, 230});
            Text(nm, P.x - tw - 12, m.y - 10, 16, RAYWHITE);
        }
    }

    int rows = (PAL_COUNT + PAL_COLS - 1) / PAL_COLS;
    float fy = gy + rows * (sw + gap) + 6;
    Text(TextFormat("Brush: %d", *brush), P.x + 12, fy, 16, (Color){200, 205, 215, 255});
    float bf = (float)*brush;
    GuiSliderBar((Rectangle){P.x + 12, fy + 22, P.width - 24, 16}, NULL, NULL, &bf, 0, (float)brushMax);
    *brush = (int)(bf + 0.5f);
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
