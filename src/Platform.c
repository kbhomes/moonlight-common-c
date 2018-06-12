#include "PlatformThreads.h"
#include "Platform.h"

#include <enet/enet.h>

int initializePlatformSockets(void);
void cleanupPlatformSockets(void);

struct thread_context {
    ThreadEntry entry;
    void* context;
#if defined(__vita__)
    PLT_THREAD* thread;
#endif
};

static int running_threads = 0;

#if defined(LC_WINDOWS)
void LimelogWindows(char* Format, ...) {
    va_list va;
    char buffer[1024];

    va_start(va, Format);
    vsprintf(buffer, Format, va);
    va_end(va);

    OutputDebugStringA(buffer);
}
#endif

#if defined(LC_WINDOWS)
DWORD WINAPI ThreadProc(LPVOID lpParameter) {
    struct thread_context* ctx = (struct thread_context*)lpParameter;
#elif defined(__vita__)
int ThreadProc(SceSize args, void *argp) {
    struct thread_context* ctx = (struct thread_context*)argp;
#elif defined(__SWITCH__)
void ThreadProc(void* context) {
    struct thread_context* ctx = (struct thread_context*)context;
#else
void* ThreadProc(void* context) {
    struct thread_context* ctx = (struct thread_context*)context;
#endif

    ctx->entry(ctx->context);

#if defined(__vita__)
    ctx->thread->alive = 0;
#else
    free(ctx);
#endif

#if defined(LC_WINDOWS) || defined(__vita__)
    return 0;
#elif defined(__SWITCH__)
    // no return
#else
    return NULL;
#endif
}

void PltSleepMs(int ms) {
#if defined(LC_WINDOWS)
    WaitForSingleObjectEx(GetCurrentThread(), ms, FALSE);
#elif defined(__vita__)
    sceKernelDelayThread(ms * 1000);
#else
    useconds_t usecs = ms * 1000;
    usleep(usecs);
#endif
}

int PltCreateMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    *mutex = CreateMutexEx(NULL, NULL, 0, MUTEX_ALL_ACCESS);
    if (!*mutex) {
        return -1;
    }
    return 0;
#elif defined(__vita__)
    *mutex = sceKernelCreateMutex("", 0, 0, NULL);
    if (*mutex < 0) {
        return -1;
    }
    return 0;
#elif defined(__SWITCH__)
    mutexInit(mutex);

    if (!*mutex) {
        return -1;
    }
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

void PltDeleteMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    CloseHandle(*mutex);
#elif defined(__vita__)
    sceKernelDeleteMutex(*mutex);
#elif defined(__SWITCH__)
    /// TODO: no ability to "close" a mutex with libnx?
#else
    pthread_mutex_destroy(mutex);
#endif
}

void PltLockMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    int err;
    err = WaitForSingleObjectEx(*mutex, INFINITE, FALSE);
    if (err != WAIT_OBJECT_0) {
        LC_ASSERT(FALSE);
    }
#elif defined(__vita__)
    sceKernelLockMutex(*mutex, 1, NULL);
#elif defined(__SWITCH__)
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

void PltUnlockMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    ReleaseMutex(*mutex);
#elif defined(__vita__)
    sceKernelUnlockMutex(*mutex, 1);
#elif defined(__SWITCH__)
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void PltJoinThread(PLT_THREAD* thread) {
    LC_ASSERT(thread->cancelled);
#if defined(LC_WINDOWS)
    WaitForSingleObjectEx(thread->handle, INFINITE, FALSE);
#elif defined(__vita__)
    while(thread->alive) {
        PltSleepMs(10);
    }
    if (thread->context != NULL)
        free(thread->context);
#elif defined(__SWITCH__)
    threadWaitForExit(&thread->thread);
#else
    pthread_join(thread->thread, NULL);
#endif
}

void PltCloseThread(PLT_THREAD* thread) {
	running_threads--;
#if defined(LC_WINDOWS)
    CloseHandle(thread->handle);
#elif defined(__vita__)
    sceKernelDeleteThread(thread->handle);
#elif defined(__SWITCH__)
    threadClose(&thread->thread);
#endif
}

int PltIsThreadInterrupted(PLT_THREAD* thread) {
    return thread->cancelled;
}

void PltInterruptThread(PLT_THREAD* thread) {
    thread->cancelled = 1;
}

int PltCreateThread(ThreadEntry entry, void* context, PLT_THREAD* thread) {
    struct thread_context* ctx;

    ctx = (struct thread_context*)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->entry = entry;
    ctx->context = context;
    
    thread->cancelled = 0;

#if defined(LC_WINDOWS)
    {
        thread->handle = CreateThread(NULL, 0, ThreadProc, ctx, 0, NULL);
        if (thread->handle == NULL) {
            free(ctx);
            return -1;
        }
    }
#elif defined(__vita__)
    {
        thread->alive = 1;
        thread->context = ctx;
        ctx->thread = thread;
        thread->handle = sceKernelCreateThread("", ThreadProc, 0, 0x40000, 0, 0, NULL);
        if (thread->handle < 0) {
            free(ctx);
            return -1;
        }
        sceKernelStartThread(thread->handle, sizeof(struct thread_context), ctx);
    }
#elif defined(__SWITCH__)
    int err = threadCreate(&thread->thread, ThreadProc, ctx, 0x40000, 0x2C, -2);
    if (err) {
        free(ctx);
        return err;
    }
    threadStart(&thread->thread);
#else
    {
        int err = pthread_create(&thread->thread, NULL, ThreadProc, ctx);
        if (err != 0) {
            free(ctx);
            return err;
        }
    }
#endif

	running_threads++;

    return 0;
}

int PltCreateEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    *event = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!*event) {
        return -1;
    }

    return 0;
#elif defined(__vita__)
    event->mutex = sceKernelCreateMutex("", 0, 0, NULL);
    if (event->mutex < 0) {
        return -1;
    }
    event->cond = sceKernelCreateCond("", 0, event->mutex, NULL);
    if (event->cond < 0) {
        sceKernelDeleteMutex(event->mutex);
        return -1;
    }
    event->signalled = 0;
    return 0;
#elif defined(__SWITCH__)
    mutexInit(&event->mutex);
    condvarInit(&event->cond, &event->mutex);
    event->signalled = 0;
    return 0;
#else
    pthread_mutex_init(&event->mutex, NULL);
    pthread_cond_init(&event->cond, NULL);
    event->signalled = 0;
    return 0;
#endif
}

void PltCloseEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    CloseHandle(*event);
#elif defined(__vita__)
    sceKernelDeleteCond(event->cond);
    sceKernelDeleteMutex(event->mutex);
#elif defined(__SWITCH__)
    /// TODO: no ability to close a mutex or a cond variable in libnx?
#else
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
#endif
}

void PltSetEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    SetEvent(*event);
#elif defined(__vita__)
    sceKernelLockMutex(event->mutex, 1, NULL);
    event->signalled = 1;
    sceKernelUnlockMutex(event->mutex, 1);
    sceKernelSignalCondAll(event->cond);
#elif defined(__SWITCH__)
    mutexLock(&event->mutex);
    event->signalled = 1;
    mutexUnlock(&event->mutex);
    condvarWakeAll(&event->cond);
#else
    pthread_mutex_lock(&event->mutex);
    event->signalled = 1;
    pthread_mutex_unlock(&event->mutex);
    pthread_cond_broadcast(&event->cond);
#endif
}

void PltClearEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    ResetEvent(*event);
#else
    event->signalled = 0;
#endif
}

int PltWaitForEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    DWORD error;

    error = WaitForSingleObjectEx(*event, INFINITE, FALSE);
    if (error == WAIT_OBJECT_0) {
        return PLT_WAIT_SUCCESS;
    }
    else {
        LC_ASSERT(0);
        return -1;
    }
#elif defined(__vita__)
    sceKernelLockMutex(event->mutex, 1, NULL);
    while (!event->signalled) {
        sceKernelWaitCond(event->cond, NULL);
    }
    sceKernelUnlockMutex(event->mutex, 1);

    return PLT_WAIT_SUCCESS;
#elif defined(__SWITCH__)
    mutexLock(&event->mutex);
    while (!event->signalled) {
        condvarWait(&event->cond);
    }
    mutexUnlock(&event->mutex);

    return PLT_WAIT_SUCCESS;
#else
    pthread_mutex_lock(&event->mutex);
    while (!event->signalled) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
    pthread_mutex_unlock(&event->mutex);
    
    return PLT_WAIT_SUCCESS;
#endif
}

uint64_t PltGetMillis(void) {
#if defined(LC_WINDOWS)
    return GetTickCount64();
#elif HAVE_CLOCK_GETTIME
    struct timespec tv;
    
    clock_gettime(CLOCK_MONOTONIC, &tv);
    
    return (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

int initializePlatform(void) {
    int err;

    err = initializePlatformSockets();
    if (err != 0) {
        return err;
    }
    
    err = enet_initialize();
    if (err != 0) {
        return err;
    }

	return 0;
}

void cleanupPlatform(void) {
    cleanupPlatformSockets();
    
    enet_deinitialize();

	LC_ASSERT(running_threads == 0);
}
