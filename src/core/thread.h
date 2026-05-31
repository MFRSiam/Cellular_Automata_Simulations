//
// thread.h - a minimal cross-platform thread + mutex wrapper.
//
// Just enough to run the simulation on a background worker while the main
// thread handles input and rendering. The platform headers (<windows.h> /
// <pthread.h>) are kept entirely inside thread.c so this header can be included
// next to raylib.h without name clashes (windows.h defines Rectangle,
// CloseWindow, ShowCursor, ... which collide with raylib).
//
#ifndef THREAD_H
#define THREAD_H

#include <stdbool.h>

// Opaque handles. `impl`/`h` point at platform objects allocated in thread.c.
typedef struct Thread { void *h; bool started; } Thread;
typedef struct Mutex  { void *impl; } Mutex;

typedef void (*ThreadEntry)(void *arg);

// Spawn `fn(arg)` on a new thread. Returns false if the thread couldn't start.
bool Thread_Start(Thread *t, ThreadEntry fn, void *arg);
// Wait for the thread to finish and release its handle.
void Thread_Join(Thread *t);

void Mutex_Init(Mutex *m);
void Mutex_Lock(Mutex *m);
void Mutex_Unlock(Mutex *m);
void Mutex_Destroy(Mutex *m);

// Sleep the calling thread for `ms` milliseconds.
void Thread_SleepMs(int ms);

#endif // THREAD_H
