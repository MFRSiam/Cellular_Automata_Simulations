//
// sim.c - multithreaded background simulation (see sim.h).
//
// Architecture:
//   * One COORDINATOR thread paces the simulation at the target rate (sleeping
//     only the remainder of each step, not a fixed interval on top).
//   * A persistent POOL of helper threads executes column strips. Each step
//     runs two phases (even strips, then odd strips - a checkerboard), so no
//     two concurrently-processed strips are adjacent: cell interactions reach
//     at most ~7 cells and strips are >= 24 wide, so phases can never race.
//   * The same pool is exposed to grid.c via GridParallelFor, which world
//     generation uses to band its passes - regen/streaming also use all cores.
//     Pool jobs are only ever published by whichever thread holds the sim
//     lock (coordinator step, or main thread during locked regeneration), so
//     the two uses cannot overlap.
//
#include "sim.h"
#include "thread.h"

#include <stdatomic.h>

#define MAX_POOL 15

static Grid  *s_grid    = NULL;
static Thread s_thread;            // coordinator
static Mutex  s_mutex;             // the public grid lock (Sim_Lock)
static volatile bool s_running = false;
static volatile bool s_active  = false;
static int    s_stepMs = 16;

// --- worker pool -------------------------------------------------------------
static Thread s_pool[MAX_POOL];
static int    s_poolN = 0;         // helper threads (coordinator participates too)
static Mutex  s_jobMx;
static Cond   s_jobCv;             // helpers wait here for a new job
static Cond   s_doneCv;            // the job publisher waits here for completion
static int    s_jobGen = 0;        // bumped to publish a job
static int    s_jobsLeft = 0;      // helpers still working the current job
static volatile bool s_poolRun = false;

static void (*s_jobFn)(int, void *) = NULL;
static void *s_jobUd = NULL;
static int   s_jobCount = 0;
static atomic_int s_jobNext;

// Pull work items until the current job is exhausted.
static void DrainJob(void) {
    for (;;) {
        int k = atomic_fetch_add(&s_jobNext, 1);
        if (k >= s_jobCount) return;
        s_jobFn(k, s_jobUd);
    }
}

static void PoolWorker(void *arg) {
    (void)arg;
    int seenGen = 0;
    for (;;) {
        Mutex_Lock(&s_jobMx);
        while (s_poolRun && s_jobGen == seenGen) Cond_Wait(&s_jobCv, &s_jobMx);
        if (!s_poolRun) { Mutex_Unlock(&s_jobMx); return; }
        seenGen = s_jobGen;
        Mutex_Unlock(&s_jobMx);

        DrainJob();

        Mutex_Lock(&s_jobMx);
        if (--s_jobsLeft == 0) Cond_Broadcast(&s_doneCv);
        Mutex_Unlock(&s_jobMx);
    }
}

// Run fn(0..count-1) across the pool plus the calling thread; returns when all
// items are done.
static void RunJob(void (*fn)(int, void *), void *ud, int count) {
    s_jobFn = fn; s_jobUd = ud; s_jobCount = count;
    atomic_store(&s_jobNext, 0);

    Mutex_Lock(&s_jobMx);
    s_jobsLeft = s_poolN;
    s_jobGen++;
    Cond_Broadcast(&s_jobCv);
    Mutex_Unlock(&s_jobMx);

    DrainJob(); // the publisher participates too

    Mutex_Lock(&s_jobMx);
    while (s_jobsLeft > 0) Cond_Wait(&s_doneCv, &s_jobMx);
    Mutex_Unlock(&s_jobMx);
}

// --- strip job for the cell update --------------------------------------------
static int s_stripW = 64, s_stripCount = 1, s_parity = 0;

static void StripJob(int k, void *ud) {
    (void)ud;
    int idx = s_parity + 2 * k;
    if (idx >= s_stripCount) return;
    int x0 = idx * s_stripW;
    int x1 = x0 + s_stripW;
    if (x1 > s_grid->width) x1 = s_grid->width;
    GridUpdateStrip(s_grid, x0, x1);
}

static void StepOnce(void) {
    GridUpdatePrepare(s_grid);
    if (s_poolN > 0 && s_stripCount > 2) {
        for (int phase = 0; phase < 2; phase++) {       // checkerboard phases
            s_parity = phase;
            int jobs = (s_stripCount - phase + 1) / 2;
            if (jobs > 0) RunJob(StripJob, NULL, jobs);
        }
    } else {
        GridUpdateStrip(s_grid, 0, s_grid->width);
    }
    GridUpdateFinish(s_grid);
}

static void Coordinator(void *arg) {
    (void)arg;
    while (s_running) {
        if (s_active) {
            double t0 = GetTime();
            Mutex_Lock(&s_mutex);
            StepOnce();
            Mutex_Unlock(&s_mutex);
            // Sleep only the REMAINDER of the step budget - heavy steps run
            // back-to-back instead of stacking a fixed sleep on top.
            int elapsed = (int)((GetTime() - t0) * 1000.0);
            int rem = s_stepMs - elapsed;
            if (rem > 0) Thread_SleepMs(rem);
        } else {
            Thread_SleepMs(8); // idle politely while paused / in menus
        }
    }
}

static void SimParallelFor(int count, void (*fn)(int, void *), void *ud) {
    RunJob(fn, ud, count);
}

void Sim_Start(Grid *grid, int targetFps) {
    s_grid   = grid;
    s_stepMs = targetFps > 0 ? (1000 / targetFps) : 16;
    Mutex_Init(&s_mutex);
    Mutex_Init(&s_jobMx);
    Cond_Init(&s_jobCv);
    Cond_Init(&s_doneCv);

    // Helpers: leave two logical cores for the main thread + OS/driver.
    int want = Thread_HWThreads() - 2;
    if (want < 0) want = 0;
    if (want > MAX_POOL) want = MAX_POOL;
    s_poolRun = true;
    s_poolN = 0;
    for (int i = 0; i < want; i++) {
        if (!Thread_Start(&s_pool[i], PoolWorker, NULL)) break;
        s_poolN++;
    }

    // Strip layout: several strips per thread per phase for load balance,
    // never narrower than 24 cells (> 2x the max interaction reach).
    int threads = s_poolN + 1;
    s_stripW = grid->width / (threads * 4);
    if (s_stripW < 24)  s_stripW = 24;
    if (s_stripW > 256) s_stripW = 256;
    s_stripCount = (grid->width + s_stripW - 1) / s_stripW;

    GridParallelFor = SimParallelFor; // world generation uses the pool too

    s_running = true;
    s_active  = false;
    if (!Thread_Start(&s_thread, Coordinator, NULL)) {
        s_running = false; // caller falls back to single-threaded stepping
    }
    TraceLog(LOG_INFO, "SIM: %d helper(s) + coordinator, %d strips of %d cells",
             s_poolN, s_stripCount, s_stripW);
}

void Sim_Stop(void) {
    if (s_running) {
        s_running = false;
        Thread_Join(&s_thread);
    }
    GridParallelFor = NULL;
    if (s_poolRun) {
        Mutex_Lock(&s_jobMx);
        s_poolRun = false;
        Cond_Broadcast(&s_jobCv);
        Mutex_Unlock(&s_jobMx);
        for (int i = 0; i < s_poolN; i++) Thread_Join(&s_pool[i]);
        s_poolN = 0;
        Mutex_Destroy(&s_mutex);
        Mutex_Destroy(&s_jobMx);
        Cond_Destroy(&s_jobCv);
        Cond_Destroy(&s_doneCv);
    }
    s_grid = NULL;
}

void Sim_SetActive(bool active) { s_active = active; }

void Sim_Lock(void)   { if (s_running) Mutex_Lock(&s_mutex); }
void Sim_Unlock(void) { if (s_running) Mutex_Unlock(&s_mutex); }

bool Sim_IsThreaded(void) { return s_running; }
