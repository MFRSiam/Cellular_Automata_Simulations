//
// strmgr.h - the Structure Manager scene.
//
// A full-screen browser (built with raygui) for the hand-made structures the
// player has captured. It shows a scrollable grid of thumbnails and offers a
// button to go capture a new one in-world.
//
#ifndef STRMGR_H
#define STRMGR_H

typedef enum StrMgrAction {
    STRMGR_NONE,
    STRMGR_BACK,   // return to the main menu
    STRMGR_NEW,    // go into the world in capture mode to make a new structure
} StrMgrAction;

void StrMgr_Init(void);     // build thumbnails for the loaded structures
void StrMgr_Refresh(void);  // rebuild after the registry changed
void StrMgr_Free(void);

// Draw and handle one frame of the manager. Call between Begin/EndDrawing.
StrMgrAction StrMgr_Draw(int screenW, int screenH);

#endif // STRMGR_H
