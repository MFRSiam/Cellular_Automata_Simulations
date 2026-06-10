//
// thread.c - platform implementation of the thread.h wrapper.
//
// All platform headers are confined to this file so thread.h stays clean.
//
#include "thread.h"

#include <stdlib.h>

// A small heap box so the platform start routine can recover fn+arg.
typedef struct ThreadStart { ThreadEntry fn; void *arg; } ThreadStart;

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static DWORD WINAPI ThreadThunk(LPVOID p) {
    ThreadStart s = *(ThreadStart *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}

bool Thread_Start(Thread *t, ThreadEntry fn, void *arg) {
    ThreadStart *s = malloc(sizeof *s);
    if (!s) return false;
    s->fn = fn; s->arg = arg;
    HANDLE h = CreateThread(NULL, 0, ThreadThunk, s, 0, NULL);
    if (!h) { free(s); t->h = NULL; t->started = false; return false; }
    t->h = h; t->started = true;
    return true;
}

void Thread_Join(Thread *t) {
    if (t->started && t->h) {
        WaitForSingleObject((HANDLE)t->h, INFINITE);
        CloseHandle((HANDLE)t->h);
    }
    t->h = NULL; t->started = false;
}

void Mutex_Init(Mutex *m) {
    CRITICAL_SECTION *cs = malloc(sizeof *cs);
    InitializeCriticalSection(cs);
    m->impl = cs;
}
void Mutex_Lock(Mutex *m)    { EnterCriticalSection((CRITICAL_SECTION *)m->impl); }
void Mutex_Unlock(Mutex *m)  { LeaveCriticalSection((CRITICAL_SECTION *)m->impl); }
void Mutex_Destroy(Mutex *m) {
    if (m->impl) { DeleteCriticalSection((CRITICAL_SECTION *)m->impl); free(m->impl); m->impl = NULL; }
}

void Cond_Init(Cond *c) {
    CONDITION_VARIABLE *cv = malloc(sizeof *cv);
    InitializeConditionVariable(cv);
    c->impl = cv;
}
void Cond_Wait(Cond *c, Mutex *m) {
    SleepConditionVariableCS((CONDITION_VARIABLE *)c->impl, (CRITICAL_SECTION *)m->impl, INFINITE);
}
void Cond_Broadcast(Cond *c) { WakeAllConditionVariable((CONDITION_VARIABLE *)c->impl); }
void Cond_Destroy(Cond *c)   { free(c->impl); c->impl = NULL; }

int Thread_HWThreads(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    return n > 0 ? n : 1;
}

void Thread_SleepMs(int ms)  { Sleep((DWORD)ms); }

#else

#include <pthread.h>
#include <unistd.h>

static void *ThreadThunk(void *p) {
    ThreadStart s = *(ThreadStart *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

bool Thread_Start(Thread *t, ThreadEntry fn, void *arg) {
    ThreadStart *s = malloc(sizeof *s);
    pthread_t *th = malloc(sizeof *th);
    if (!s || !th) { free(s); free(th); return false; }
    s->fn = fn; s->arg = arg;
    if (pthread_create(th, NULL, ThreadThunk, s) != 0) { free(s); free(th); return false; }
    t->h = th; t->started = true;
    return true;
}

void Thread_Join(Thread *t) {
    if (t->started && t->h) { pthread_join(*(pthread_t *)t->h, NULL); free(t->h); }
    t->h = NULL; t->started = false;
}

void Mutex_Init(Mutex *m) {
    pthread_mutex_t *mx = malloc(sizeof *mx);
    pthread_mutex_init(mx, NULL);
    m->impl = mx;
}
void Mutex_Lock(Mutex *m)    { pthread_mutex_lock((pthread_mutex_t *)m->impl); }
void Mutex_Unlock(Mutex *m)  { pthread_mutex_unlock((pthread_mutex_t *)m->impl); }
void Mutex_Destroy(Mutex *m) {
    if (m->impl) { pthread_mutex_destroy((pthread_mutex_t *)m->impl); free(m->impl); m->impl = NULL; }
}

void Cond_Init(Cond *c) {
    pthread_cond_t *cv = malloc(sizeof *cv);
    pthread_cond_init(cv, NULL);
    c->impl = cv;
}
void Cond_Wait(Cond *c, Mutex *m) {
    pthread_cond_wait((pthread_cond_t *)c->impl, (pthread_mutex_t *)m->impl);
}
void Cond_Broadcast(Cond *c) { pthread_cond_broadcast((pthread_cond_t *)c->impl); }
void Cond_Destroy(Cond *c) {
    if (c->impl) { pthread_cond_destroy((pthread_cond_t *)c->impl); free(c->impl); c->impl = NULL; }
}

int Thread_HWThreads(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

void Thread_SleepMs(int ms)  { usleep((useconds_t)ms * 1000); }

#endif
