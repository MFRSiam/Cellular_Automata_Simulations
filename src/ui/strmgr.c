//
// strmgr.c - Structure Manager scene (see strmgr.h).
//
#include "strmgr.h"
#include "core.h"
#include "structure.h"
#include "materials.h"

#include "raygui.h"

#include <math.h>

#define MAX_THUMBS MAX_STRUCTURES

static struct {
    Texture2D thumb[MAX_THUMBS];
    int       w[MAX_THUMBS], h[MAX_THUMBS];
    int       count;
    float     scroll;   // vertical scroll offset in pixels
    bool      built;
} M;

// Render a structure's cells into a small texture (point-filtered, so it stays
// crisp and pixelated when scaled up into a card).
static Texture2D BuildThumb(const Structure *s) {
    Image img = GenImageColor(s->w, s->h, (Color){0, 0, 0, 0});
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            Cell c = s->cells[y * s->w + x];
            if (c != CELL_EMPTY) ImageDrawPixel(&img, x, y, MATERIALS[c].color);
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    return t;
}

void StrMgr_Init(void) {
    M.count = Structure_Count();
    if (M.count > MAX_THUMBS) M.count = MAX_THUMBS;
    for (int i = 0; i < M.count; i++) {
        const Structure *s = Structure_Get(i);
        M.thumb[i] = BuildThumb(s);
        M.w[i] = s->w; M.h[i] = s->h;
    }
    M.scroll = 0.0f;
    M.built = true;
}

void StrMgr_Free(void) {
    if (!M.built) return;
    for (int i = 0; i < M.count; i++) UnloadTexture(M.thumb[i]);
    M.count = 0;
    M.built = false;
}

void StrMgr_Refresh(void) {
    StrMgr_Free();
    StrMgr_Init();
}

// Draw a single thumbnail card at (x,y) sized cell x cell.
static void DrawCard(int index, float x, float y, float cell) {
    Rectangle card = {x, y, cell, cell};
    DrawRectangleRounded(card, 0.06f, 6, (Color){26, 30, 40, 255});
    DrawRectangleRoundedLines(card, 0.06f, 6, (Color){70, 90, 120, 200});

    // A subtle checker so transparent (empty) areas of the structure read.
    float pad = 10.0f, thumbBox = cell - pad * 2 - 22;
    Rectangle box = {x + pad, y + pad, thumbBox, thumbBox};
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 8; cx++) {
            Color sq = ((cx + cy) & 1) ? (Color){18, 20, 26, 255} : (Color){12, 13, 17, 255};
            DrawRectangle((int)(box.x + cx * box.width / 8), (int)(box.y + cy * box.height / 8),
                          (int)ceilf(box.width / 8), (int)ceilf(box.height / 8), sq);
        }

    // Fit the structure into the box preserving aspect ratio.
    float sw = (float)M.w[index], sh = (float)M.h[index];
    float scale = fminf(box.width / sw, box.height / sh);
    float dw = sw * scale, dh = sh * scale;
    Rectangle dst = {box.x + (box.width - dw) / 2, box.y + (box.height - dh) / 2, dw, dh};
    DrawTexturePro(M.thumb[index], (Rectangle){0, 0, sw, sh}, dst, (Vector2){0, 0}, 0, WHITE);

    const char *label = TextFormat("#%d   %dx%d", index, M.w[index], M.h[index]);
    DrawText(label, (int)(x + pad), (int)(y + cell - 20), 16, (Color){190, 200, 215, 255});
}

StrMgrAction StrMgr_Draw(int w, int h) {
    StrMgrAction action = STRMGR_NONE;

    // Background.
    DrawRectangleGradientV(0, 0, w, h, (Color){20, 24, 34, 255}, (Color){8, 9, 14, 255});

    // Header bar.
    DrawText("STRUCTURE MANAGER", 28, 26, 34, (Color){235, 240, 255, 255});
    DrawText(TextFormat("%d saved structure%s", M.count, M.count == 1 ? "" : "s"),
             30, 66, 18, (Color){120, 150, 190, 255});

    // Top-right action buttons (raygui).
    if (GuiButton((Rectangle){(float)w - 360, 28, 160, 40}, "New Structure")) action = STRMGR_NEW;
    if (GuiButton((Rectangle){(float)w - 190, 28, 160, 40}, "Back"))          action = STRMGR_BACK;

    // Scrollable grid region.
    Rectangle area = {24, 100, (float)w - 48, (float)h - 124};
    float cell = 180.0f, gap = 18.0f;
    int cols = (int)((area.width + gap) / (cell + gap));
    if (cols < 1) cols = 1;
    int rows = (M.count + cols - 1) / cols;
    float contentH = rows * (cell + gap);

    // Wheel scroll, clamped to content.
    if (CheckCollisionPointRec(GetMousePosition(), area))
        M.scroll -= GetMouseWheelMove() * 48.0f;
    float maxScroll = contentH - area.height;
    if (maxScroll < 0) maxScroll = 0;
    if (M.scroll < 0) M.scroll = 0;
    if (M.scroll > maxScroll) M.scroll = maxScroll;

    if (M.count == 0) {
        DrawText("No structures yet. Click \"New Structure\" to open the editor.",
                 (int)area.x + 8, (int)area.y + 8, 20, (Color){150, 160, 175, 255});
        return action;
    }

    int pendingDelete = -1;
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    for (int i = 0; i < M.count; i++) {
        int cx = i % cols, cy = i / cols;
        float x = area.x + cx * (cell + gap);
        float y = area.y + cy * (cell + gap) - M.scroll;
        if (y + cell < area.y || y > area.y + area.height) continue; // cull off-screen
        DrawCard(i, x, y, cell);
        // Per-card delete button (top-right corner).
        if (GuiButton((Rectangle){x + cell - 34, y + 8, 26, 26}, "X")) pendingDelete = i;
    }
    EndScissorMode();

    // Apply a delete after the loop so indices stay stable while drawing.
    if (pendingDelete >= 0) {
        Structure_Delete(pendingDelete);
        StrMgr_Refresh();
        return action;
    }

    // Scrollbar hint.
    if (maxScroll > 0) {
        float trackH = area.height;
        float thumbH = trackH * (area.height / contentH);
        float thumbY = area.y + (M.scroll / maxScroll) * (trackH - thumbH);
        DrawRectangleRounded((Rectangle){area.x + area.width - 6, thumbY, 5, thumbH},
                             1.0f, 4, (Color){90, 110, 140, 180});
    }
    return action;
}
