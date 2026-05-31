//
// editor.h - the Structure Editor scene.
//
// A standalone black canvas you paint materials onto. The full cellular-
// automata simulation runs on it, so materials interact (fall, burn, freeze,
// dissolve, grow...). A side panel shows live stats - total used pixels and a
// per-material breakdown - and buttons to play/pause, clear, save and exit.
//
#ifndef EDITOR_H
#define EDITOR_H

typedef enum EditorAction {
    EDITOR_STAY,
    EDITOR_BACK,   // leave the editor (return to the Structure Manager)
} EditorAction;

void Editor_Open(void);   // allocate a fresh, empty canvas
void Editor_Close(void);  // free the canvas

// Run one frame: input + simulation + drawing. Call between Begin/EndDrawing.
EditorAction Editor_Run(int screenW, int screenH);

#endif // EDITOR_H
