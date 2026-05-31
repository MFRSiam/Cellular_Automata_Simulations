//
// editor.c - Structure Editor scene (see editor.h).
//
#include "editor.h"
#include "core.h"
#include "grid.h"
#include "materials.h"
#include "structure.h"

#include "raygui.h"

#include <math.h>
#include <stdio.h>

#define CANVAS   96     // canvas is CANVAS x CANVAS cells (<= structure limit)
#define PANEL_W  300.0f // width of the right-hand stats / tools panel

static struct {
    Grid     grid;
    bool     open;
    bool     simulate;   // is the sim running (Space toggles)
    Cell     selected;
    int      brush;
    char     status[96];
    float    statusT;    // seconds remaining on the status toast
    int      counts[CELL_COUNT];
    int      used;       // total non-empty cells
} E;

void Editor_Open(void) {
    if (!E.open) {
        E.grid = GridCreate(CANVAS * CELL_SIZE, CANVAS * CELL_SIZE);
        E.open = true;
    }
    GridClear(&E.grid);
    E.grid.originX = 0; E.grid.originY = 0;
    E.simulate = true;
    E.selected = CELL_SAND;
    E.brush = 2;
    E.status[0] = '\0';
    E.statusT = 0.0f;
}

void Editor_Close(void) {
    if (E.open) { GridFree(&E.grid); E.open = false; }
}

static void SetStatus(const char *s) {
    snprintf(E.status, sizeof E.status, "%s", s);
    E.statusT = 2.5f;
}

// Tally how many of each material is currently on the canvas.
static void CountCells(void) {
    for (int i = 0; i < CELL_COUNT; i++) E.counts[i] = 0;
    E.used = 0;
    int n = E.grid.width * E.grid.height;
    for (int i = 0; i < n; i++) {
        Cell c = E.grid.cells[i];
        E.counts[c]++;
        if (c != CELL_EMPTY) E.used++;
    }
}

// Trim to the non-empty bounding box and save as a structure.
static void SaveCanvas(void) {
    int minX = CANVAS, minY = CANVAS, maxX = -1, maxY = -1;
    for (int y = 0; y < E.grid.height; y++)
        for (int x = 0; x < E.grid.width; x++)
            if (E.grid.cells[y * E.grid.width + x] != CELL_EMPTY) {
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
    if (maxX < 0) { SetStatus("Canvas is empty"); return; }

    int w = maxX - minX + 1, h = maxY - minY + 1;
    Cell *buf = MemAlloc((unsigned)(w * h) * sizeof(Cell));
    for (int ly = 0; ly < h; ly++)
        for (int lx = 0; lx < w; lx++)
            buf[ly * w + lx] = E.grid.cells[(minY + ly) * E.grid.width + (minX + lx)];

    char path[512];
    snprintf(path, sizeof path, "%sstructures/struct_%u.txt",
             GetApplicationDirectory(), (unsigned)GetRandomValue(1000, 999999));
    bool ok = Structure_AddAndSave(w, h, buf, path);
    MemFree(buf);
    SetStatus(ok ? TextFormat("Saved %dx%d structure", w, h) : "Save failed");
}

// Keyboard material selection (mirrors the in-game palette, plus a couple of
// extras handy for showing off the science reactions).
static void HandleMaterialKeys(void) {
    static const Cell pal[] = {
        CELL_SAND, CELL_WATER, CELL_WOOD, CELL_OIL, CELL_ACID,
        CELL_SNOW, CELL_FIRE, CELL_GAS, CELL_VAPOR,
    };
    for (int k = 0; k < 9; k++)
        if (IsKeyPressed(KEY_ONE + k)) E.selected = pal[k];
    if (IsKeyPressed(KEY_ZERO)) E.selected = CELL_EMPTY;
    if (IsKeyPressed(KEY_L)) E.selected = CELL_LAVA;
    if (IsKeyPressed(KEY_R)) E.selected = CELL_ROCK;
    if (IsKeyPressed(KEY_M)) E.selected = CELL_MUD;
    if (IsKeyPressed(KEY_V)) E.selected = CELL_GLASS;
    if (IsKeyPressed(KEY_B)) E.selected = CELL_METAL;
    if (IsKeyPressed(KEY_N)) E.selected = CELL_OBSIDIAN;
    if (IsKeyPressed(KEY_K)) E.selected = CELL_MOSS;
    if (IsKeyPressed(KEY_I)) E.selected = CELL_ICE;
    if (IsKeyPressed(KEY_U)) E.selected = CELL_INERT_GAS;
    if (IsKeyPressed(KEY_Y)) E.selected = CELL_ACID_GAS;
    if (IsKeyPressed(KEY_T)) E.selected = CELL_GRASS;
    if (IsKeyPressed(KEY_J)) E.selected = CELL_VINE;
    if (IsKeyPressed(KEY_O)) E.selected = CELL_GOLD;
    if (IsKeyPressed(KEY_P)) E.selected = CELL_COPPER;
    if (IsKeyPressed(KEY_X)) E.selected = CELL_CRYSTAL;
    if (IsKeyPressed(KEY_F)) E.selected = CELL_SPARK;
    if (IsKeyPressed(KEY_H)) E.selected = CELL_GUNPOWDER;
    if (IsKeyPressed(KEY_Z)) E.selected = CELL_MERCURY;
    // TAB cycles through every material (covers ones without a hotkey).
    if (IsKeyPressed(KEY_TAB)) E.selected = (Cell)(E.selected % (CELL_COUNT - 1) + 1);
}

static void DrawStatsPanel(int screenW, int screenH, EditorAction *action) {
    float px = screenW - PANEL_W;
    DrawRectangle((int)px, 0, (int)PANEL_W, screenH, (Color){16, 18, 24, 255});
    DrawRectangleLines((int)px, 0, (int)PANEL_W, screenH, (Color){60, 70, 90, 255});

    float x = px + 18.0f, y = 16.0f;
    DrawText("STRUCTURE EDITOR", (int)x, (int)y, 22, (Color){235, 240, 255, 255}); y += 34;

    // Selected material + brush.
    DrawText("Material:", (int)x, (int)y, 16, (Color){150, 160, 175, 255});
    DrawRectangle((int)(x + 86), (int)y - 2, 18, 18, MATERIALS[E.selected].color);
    DrawRectangleLines((int)(x + 86), (int)y - 2, 18, 18, (Color){90, 100, 120, 255});
    DrawText(CellName(E.selected), (int)(x + 112), (int)y, 16, (Color){220, 225, 235, 255});
    y += 24;
    DrawText(TextFormat("Brush: %d    Sim: %s", E.brush, E.simulate ? "ON" : "OFF"),
             (int)x, (int)y, 16, (Color){150, 160, 175, 255});
    y += 30;

    // Live stats.
    DrawText(TextFormat("Pixels used: %d", E.used), (int)x, (int)y, 18, (Color){150, 200, 160, 255});
    y += 28;
    DrawText("Composition:", (int)x, (int)y, 16, (Color){150, 160, 175, 255});
    y += 24;
    for (int c = 1; c < CELL_COUNT; c++) {
        if (E.counts[c] == 0) continue;
        DrawRectangle((int)x, (int)y, 14, 14, MATERIALS[c].color);
        DrawRectangleLines((int)x, (int)y, 14, 14, (Color){80, 90, 110, 255});
        float pct = E.used ? (100.0f * E.counts[c] / E.used) : 0.0f;
        DrawText(TextFormat("%-10s %5d  %4.1f%%", CellName(c), E.counts[c], pct),
                 (int)(x + 22), (int)y, 15, (Color){210, 215, 225, 255});
        y += 19;
    }

    // Buttons along the bottom of the panel.
    float bw = PANEL_W - 36, bx = px + 18, bh = 40, gap = 10;
    float by = screenH - (bh + gap) * 4 - 14;
    if (GuiButton((Rectangle){bx, by + 0 * (bh + gap), bw, bh},
                  E.simulate ? "Pause Simulation" : "Play Simulation")) E.simulate = !E.simulate;
    if (GuiButton((Rectangle){bx, by + 1 * (bh + gap), bw, bh}, "Clear Canvas")) { GridClear(&E.grid); SetStatus("Cleared"); }
    if (GuiButton((Rectangle){bx, by + 2 * (bh + gap), bw, bh}, "Save Structure")) SaveCanvas();
    if (GuiButton((Rectangle){bx, by + 3 * (bh + gap), bw, bh}, "Back")) *action = EDITOR_BACK;

    // Controls hint + status toast.
    DrawText("materials: 1-9 L R M V B N U Y T J O P X I K F H Z   TAB cycle   0 erase   wheel brush   Space play/pause",
             18, screenH - 26, 14, (Color){120, 130, 145, 255});
    if (E.statusT > 0.0f)
        DrawText(E.status, 18, screenH - 50, 18, Fade((Color){255, 230, 140, 255}, fminf(E.statusT, 1.0f)));
}

EditorAction Editor_Run(int screenW, int screenH) {
    EditorAction action = EDITOR_STAY;
    float dt = GetFrameTime();
    float canvasViewW = screenW - PANEL_W;

    // --- camera fits the whole canvas into the left viewport ----------------
    float canvasPx = CANVAS * CELL_SIZE;
    float zoom = fminf(canvasViewW / canvasPx, screenH / canvasPx) * 0.92f;
    Camera2D cam = {
        .target = {canvasPx * 0.5f, canvasPx * 0.5f},
        .offset = {canvasViewW * 0.5f, screenH * 0.5f},
        .zoom   = zoom,
    };

    // --- input --------------------------------------------------------------
    HandleMaterialKeys();
    if (IsKeyPressed(KEY_ESCAPE)) action = EDITOR_BACK;
    if (IsKeyPressed(KEY_SPACE)) E.simulate = !E.simulate;
    E.brush += (int)GetMouseWheelMove();
    if (E.brush < 0) E.brush = 0; if (E.brush > 20) E.brush = 20;

    Vector2 m = GetMousePosition();
    bool overCanvas = m.x < canvasViewW;
    Vector2 world = GetScreenToWorld2D(m, cam);
    int cx = (int)floorf(world.x / CELL_SIZE), cy = (int)floorf(world.y / CELL_SIZE);
    if (overCanvas) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))  GridPaint(&E.grid, cx, cy, E.brush, E.selected);
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) GridPaint(&E.grid, cx, cy, E.brush, CELL_EMPTY);
    }

    // --- simulate -----------------------------------------------------------
    if (E.simulate) GridUpdate(&E.grid);
    GridSnapshot(&E.grid);
    CountCells();
    if (E.statusT > 0.0f) E.statusT -= dt;

    // --- draw the canvas ----------------------------------------------------
    BeginScissorMode(0, 0, (int)canvasViewW, screenH);
    BeginMode2D(cam);
    DrawRectangle(0, 0, (int)canvasPx, (int)canvasPx, (Color){6, 7, 9, 255}); // the "black canvas"
    GridDrawWorld(&E.grid, cam);
    DrawRectangleLinesEx((Rectangle){0, 0, canvasPx, canvasPx}, 1.5f / zoom, (Color){70, 80, 100, 255});
    if (overCanvas && E.brush > 0) // brush preview
        DrawCircleLines((cx) * CELL_SIZE + CELL_SIZE / 2, (cy) * CELL_SIZE + CELL_SIZE / 2,
                        E.brush * CELL_SIZE, Fade(RAYWHITE, 0.5f));
    EndMode2D();
    EndScissorMode();

    // --- side panel ---------------------------------------------------------
    DrawStatsPanel(screenW, screenH, &action);
    return action;
}
