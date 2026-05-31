//
// sim.c - background simulation worker (see sim.h).
//
#include "sim.h"
#include "thread.h"

static Grid  *s_grid    = NULL;
static Thread s_thread;
static Mutex  s_mutex;
static volatile bool s_running = false;  // worker should keep looping
static volatile bool s_active  = false;  // worker should step this tick
static int    s_stepMs = 16;             // target time per simulation step

static void Worker(void *arg) {
    (void)arg;
    while (s_running) {
        if (s_active) {
            Mutex_Lock(&s_mutex);
            GridUpdate(s_grid);
            Mutex_Unlock(&s_mutex);
            // Pace to roughly the target step rate. The lock is released first
            // so the main thread can snapshot / edit between steps.
            Thread_SleepMs(s_stepMs);
        } else {
            Thread_SleepMs(8); // idle politely while paused
        }
    }
}

void Sim_Start(Grid *grid, int targetFps) {
    s_grid   = grid;
    s_stepMs = targetFps > 0 ? (1000 / targetFps) : 16;
    Mutex_Init(&s_mutex);
    s_running = true;
    s_active  = false;
    if (!Thread_Start(&s_thread, Worker, NULL)) {
        s_running = false; // fall back to single-threaded stepping by the caller
    }
}

void Sim_Stop(void) {
    if (!s_running) return;
    s_running = false;
    Thread_Join(&s_thread);
    Mutex_Destroy(&s_mutex);
    s_grid = NULL;
}

void Sim_SetActive(bool active) { s_active = active; }

void Sim_Lock(void)   { if (s_running) Mutex_Lock(&s_mutex); }
void Sim_Unlock(void) { if (s_running) Mutex_Unlock(&s_mutex); }

bool Sim_IsThreaded(void) { return s_running; }
