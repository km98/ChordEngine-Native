// =============================================================================
// RtProbe.c - counts heap and lock activity on an "armed" thread.
//
// Loaded into a test with DYLD_INSERT_LIBRARIES. Its __interpose table routes
// malloc/free/realloc/calloc/posix_memalign/aligned_alloc (plain and typed) and
// the pthread mutex/rwlock/condition and os_unfair_lock entry points through
// counters, which only count while the CURRENT thread is armed. The test arms
// the thread around the audio callback it wants to prove real-time safe.
//
// The test reaches the API with dlsym (the symbols are exported explicitly),
// and a positive control (an allocation while armed) proves the probe is
// loaded and counting - a probe that silently isn't there would otherwise
// report zeros.
// =============================================================================
#pragma clang diagnostic ignored "-Wunguarded-availability-new" // test tool, macOS 14+ hosts
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

#define RTPROBE_EXPORT __attribute__((visibility("default")))

// The armed thread. A thread-local flag would itself allocate on first use
// (inside the interposed malloc), so the thread id is compared instead.
static _Atomic(pthread_t) armedThread = NULL;
#define armed (atomic_load_explicit(&armedThread, memory_order_relaxed) == pthread_self())
static atomic_long allocations = 0;
static atomic_long frees = 0;
static atomic_long locks = 0;

RTPROBE_EXPORT void ceRtProbeArm(int on) { atomic_store(&armedThread, on ? pthread_self() : NULL); }
RTPROBE_EXPORT long ceRtProbeAllocations(void) { return atomic_load(&allocations); }
RTPROBE_EXPORT long ceRtProbeFrees(void) { return atomic_load(&frees); }
RTPROBE_EXPORT long ceRtProbeLocks(void) { return atomic_load(&locks); }
RTPROBE_EXPORT void ceRtProbeReset(void)
{
    atomic_store(&allocations, 0);
    atomic_store(&frees, 0);
    atomic_store(&locks, 0);
}

static void *probe_malloc(size_t size) { if (armed) atomic_fetch_add(&allocations, 1); return malloc(size); }
static void *probe_calloc(size_t count, size_t size) { if (armed) atomic_fetch_add(&allocations, 1); return calloc(count, size); }
static void *probe_realloc(void *pointer, size_t size) { if (armed) atomic_fetch_add(&allocations, 1); return realloc(pointer, size); }
static void probe_free(void *pointer) { if (armed && pointer != NULL) atomic_fetch_add(&frees, 1); free(pointer); }
static int probe_posix_memalign(void **out, size_t alignment, size_t size)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return posix_memalign(out, alignment, size);
}
static void *probe_aligned_alloc(size_t alignment, size_t size)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return aligned_alloc(alignment, size);
}
static void *probe_malloc_zone_malloc(malloc_zone_t *zone, size_t size)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return malloc_zone_malloc(zone, size);
}

// Typed allocation (macOS 14+ SDKs compile malloc/new to these).
static void *probe_type_malloc(size_t size, malloc_type_id_t type) { if (armed) atomic_fetch_add(&allocations, 1); return malloc_type_malloc(size, type); }
static void *probe_type_calloc(size_t count, size_t size, malloc_type_id_t type) { if (armed) atomic_fetch_add(&allocations, 1); return malloc_type_calloc(count, size, type); }
static void *probe_type_realloc(void *pointer, size_t size, malloc_type_id_t type) { if (armed) atomic_fetch_add(&allocations, 1); return malloc_type_realloc(pointer, size, type); }
static void probe_type_free(void *pointer, malloc_type_id_t type) { if (armed && pointer != NULL) atomic_fetch_add(&frees, 1); malloc_type_free(pointer, type); }
static int probe_type_posix_memalign(void **out, size_t alignment, size_t size, malloc_type_id_t type)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return malloc_type_posix_memalign(out, alignment, size, type);
}
static void *probe_type_aligned_alloc(size_t alignment, size_t size, malloc_type_id_t type)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return malloc_type_aligned_alloc(alignment, size, type);
}
static void *probe_type_zone_malloc(malloc_zone_t *zone, size_t size, malloc_type_id_t type)
{
    if (armed) atomic_fetch_add(&allocations, 1);
    return malloc_type_zone_malloc(zone, size, type);
}

static int probe_mutex_lock(pthread_mutex_t *mutex) { if (armed) atomic_fetch_add(&locks, 1); return pthread_mutex_lock(mutex); }
static int probe_mutex_trylock(pthread_mutex_t *mutex) { if (armed) atomic_fetch_add(&locks, 1); return pthread_mutex_trylock(mutex); }
static int probe_rwlock_rdlock(pthread_rwlock_t *lock) { if (armed) atomic_fetch_add(&locks, 1); return pthread_rwlock_rdlock(lock); }
static int probe_rwlock_wrlock(pthread_rwlock_t *lock) { if (armed) atomic_fetch_add(&locks, 1); return pthread_rwlock_wrlock(lock); }
static int probe_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) { if (armed) atomic_fetch_add(&locks, 1); return pthread_cond_wait(cond, mutex); }
static void probe_unfair_lock(os_unfair_lock_t lock) { if (armed) atomic_fetch_add(&locks, 1); os_unfair_lock_lock(lock); }

#define INTERPOSE(replacement, original) \
    __attribute__((used)) static const struct { const void *r; const void *o; } interpose_##original \
        __attribute__((section("__DATA,__interpose"))) = { (const void *) (replacement), (const void *) (original) }

INTERPOSE(probe_malloc, malloc);
INTERPOSE(probe_calloc, calloc);
INTERPOSE(probe_realloc, realloc);
INTERPOSE(probe_free, free);
INTERPOSE(probe_posix_memalign, posix_memalign);
INTERPOSE(probe_aligned_alloc, aligned_alloc);
INTERPOSE(probe_malloc_zone_malloc, malloc_zone_malloc);
INTERPOSE(probe_type_malloc, malloc_type_malloc);
INTERPOSE(probe_type_calloc, malloc_type_calloc);
INTERPOSE(probe_type_realloc, malloc_type_realloc);
INTERPOSE(probe_type_free, malloc_type_free);
INTERPOSE(probe_type_posix_memalign, malloc_type_posix_memalign);
INTERPOSE(probe_type_aligned_alloc, malloc_type_aligned_alloc);
INTERPOSE(probe_type_zone_malloc, malloc_type_zone_malloc);
INTERPOSE(probe_mutex_lock, pthread_mutex_lock);
INTERPOSE(probe_mutex_trylock, pthread_mutex_trylock);
INTERPOSE(probe_rwlock_rdlock, pthread_rwlock_rdlock);
INTERPOSE(probe_rwlock_wrlock, pthread_rwlock_wrlock);
INTERPOSE(probe_cond_wait, pthread_cond_wait);
INTERPOSE(probe_unfair_lock, os_unfair_lock_lock);
