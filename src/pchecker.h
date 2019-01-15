#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>

#if __cplusplus >= 201103L
#include <atomic>
#endif

#ifdef __GNUC__
__attribute__((__unused__))
#endif
/* test if function pointer size  equals object pointer size
 * (likely there wont be a libdl if thats not the case) */
typedef char assert_functionpointersize[(sizeof(void *) == sizeof(void (*)())) ? 1 : -1];

#if __GNUC__ >= 4
#define DSO_PUBLIC __attribute__((visibility("default")))
#define DSO_HIDDEN __attribute__((visibility("hidden")))
#else
#define DSO_PUBLIC
#define DSO_HIDDEN
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define FUN_INLINE inline
#elif defined(__GNUC__)
#define FUN_INLINE __inline__
#else
#define FUN_INLINE
#endif

#if __cplusplus >= 201103L
#define VAR_ATOMIC(t) std::atomic<t>
#define VAR_ATOMIC_FLAG std::atomic_flag
#define VAR_ATOMIC_FLAG_TESTSET(v) std::atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) std::atomic_flag_clear(&v)

#elif __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define VAR_ATOMIC(t) _Atomic t
#define VAR_ATOMIC_FLAG atomic_flag
#define VAR_ATOMIC_FLAG_TESTSET(v) atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) atomic_flag_clear(&v)

#else
/* the compiler will not create memory barrier instructions,
 * this is not ideal but should not cause big problems. */
#define VAR_ATOMIC(t) volatile t
#define VAR_ATOMIC_FLAG volatile int
static FUN_INLINE int v_atomic_flag_test_and_set(volatile int *v)
{
#ifdef __GNUC__
    __asm__ __volatile__("" ::: "memory");
#endif
    if (!*v)
        return 1;
    *v = 1;
#ifdef __GNUC__
    __asm__ __volatile__("" ::: "memory");
#endif
    return 0;
}
static FUN_INLINE void v_atomic_flag_clear(volatile int *v)
{
#ifdef __GNUC__
    __asm__ __volatile__("" ::: "memory");
#endif
    *v = 0;
}

#define VAR_ATOMIC_FLAG_TESTSET(v) v_atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) v_atomic_flag_clear(&v)
#endif

#if __GNUC__
/* prefer not to include string.h */
#define FUN_MEMCPY(d, s, l) __builtin_memcpy((d), (s), (l))
#define FUN_TRAP() __builtin_trap()

#else
#include <stdlib.h>
#include <string.h>
#define FUN_MEMCPY(d, s, l) memcpy((d), (s), (l))
#define FUN_TRAP() abort()
#endif

#ifndef EINVAL
#define EINVAL 22 /* Invalid argument */
#endif

#if !defined(unlikely) && defined(__GNUC__)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#if defined(__GNUC__)
#define MEM_BARRIER()                          \
    do {                                       \
        __asm__ __volatile__("" ::: "memory"); \
    } while (0)
#else
#define MEM_BARRIER()
#endif

/* silence type warnings and be pedantically C conform */
#define COPY_PF(r, t, p)                       \
    {                                          \
        t temp;                                \
        FUN_MEMCPY(&temp, &(p), sizeof(temp)); \
        r = temp;                              \
    }

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pf_void_t)();

static struct resolve_state {
    VAR_ATOMIC(int) alldone;
    VAR_ATOMIC(int) state;
    VAR_ATOMIC_FLAG lock;

    void (*pf_cobalt_assert_nrt)();
} s_ResolveState;

static FUN_INLINE int initIsDone()
{
    MEM_BARRIER();
    return s_ResolveState.alldone;
}
static FUN_INLINE void setInitIsDone()
{
    MEM_BARRIER();
    s_ResolveState.alldone = 1;
    MEM_BARRIER();
}

static FUN_INLINE int setState(int set)
{
    /* at least prevent the compiler from reordering */
    MEM_BARRIER();
    if (set) {
        s_ResolveState.state = set;
        MEM_BARRIER();
        return set;
    }
    return s_ResolveState.state;
}

static FUN_INLINE int resolveIsDone()
{
    return setState(0) >= 128;
}
static FUN_INLINE int setResolveIsDone()
{
    return setState(128);
}

static FUN_INLINE int acquireLock()
{
    return !VAR_ATOMIC_FLAG_TESTSET(s_ResolveState.lock);
}

static FUN_INLINE void releaseLock()
{
    VAR_ATOMIC_FLAG_CLEAR(s_ResolveState.lock);
}

static FUN_INLINE void *getdelegate_function(const char *name)
{
    return dlsym(RTLD_NEXT, name);
}

static void noCheck() {}

static int getassert_function(int state)
{
    void *pf;

    if (state == 0) {
        s_ResolveState.pf_cobalt_assert_nrt = &noCheck;
        return 0;
    }
    pf = dlsym(RTLD_DEFAULT, "cobalt_assert_nrt");
    if (pf) {
        COPY_PF(s_ResolveState.pf_cobalt_assert_nrt, pf_void_t, pf);
        return 1;
    }

    return 0;
}

static FUN_INLINE void callAssertFunction(int check)
{
    pf_void_t pf = s_ResolveState.pf_cobalt_assert_nrt;
    if (!check || pf)
        (*pf)();
}

#ifdef __cplusplus
}
#endif
