//
// sim.h - runs the cellular-automata step on a background worker thread.
//
// The worker repeatedly calls GridUpdate() while the main thread handles input
// and rendering, so a heavy simulation no longer stalls the frame rate. All
// access to the grid from the main thread (painting, streaming, regenerating,
// snapshotting for the renderer) must be bracketed by Sim_Lock()/Sim_Unlock().
//
#ifndef SIM_H
#define SIM_H

#include "grid.h"

// Start the worker on `grid`. The worker begins paused (see Sim_SetActive).
void Sim_Start(Grid *grid, int targetFps);
// Stop and join the worker. Safe to call if never started.
void Sim_Stop(void);

// Enable/disable stepping (e.g. disable while paused or in menus).
void Sim_SetActive(bool active);

// Mutual exclusion around the shared grid.
void Sim_Lock(void);
void Sim_Unlock(void);

// True if a worker thread is actually running. When false the caller should
// step the grid itself (GridUpdate) so the sim still advances.
bool Sim_IsThreaded(void);

#endif // SIM_H
