#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>

#ifndef PCHECKER_CHECKASSERT_NAME
#define PCHECKER_CHECKASSERT_NAME "cobalt_assert_nrt"
#endif

#ifdef __GNUC__
__attribute__((__unused__))
#endif
/* test if function pointer size  equals object pointer size
 * (likely there wont be a libdl if thats not the case) */
typedef char assert_functionpointersize[(sizeof(void *) == sizeof(void (*)())) ? 1 : -1];

/* supporting C functions and definitions */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L || \
    defined(__cplusplus)
#define FUN_INLINE inline
#endif

#if __GNUC__
/* prefer not to include string.h */
#define FUN_MEMCPY(d, s, l) __builtin_memcpy((d), (s), (l))
#define FUN_TRAP() __builtin_trap()

#if !defined(unlikely)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#if __GNUC__ >= 4
#define DSO_PUBLIC __attribute__((visibility("default")))
#define DSO_HIDDEN __attribute__((visibility("hidden")))
#endif

#if !defined(FUN_INLINE)
#define FUN_INLINE __inline__
#endif

#define MEM_BARRIER()                          \
    do {                                       \
        __asm__ __volatile__("" ::: "memory"); \
    } while (0)

#else /* if __GNUC__ */
#include <stdlib.h>
#include <string.h>
#define FUN_MEMCPY(d, s, l) memcpy((d), (s), (l))
#define FUN_TRAP() abort()

#define MEM_BARRIER()

#endif /* if __GNUC__ */

#ifndef EINVAL
#define EINVAL 22 /* Invalid argument */
#endif
#if !defined(FUN_INLINE)
#define FUN_INLINE
#endif
#if !defined(DSO_PUBLIC)
#define DSO_PUBLIC
#define DSO_HIDDEN
#endif

/* silence type warnings and be pedantically C conform */
#define COPY_PF(r, t, p)                       \
    {                                          \
        t temp;                                \
        FUN_MEMCPY(&temp, &(p), sizeof(temp)); \
        r = temp;                              \
    }

/* Support for simple atomic flags,
 * prefer the C-library function even if compiling for C++ */
#if __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define VAR_ATOMIC(t) _Atomic t
#define VAR_ATOMIC_FLAG atomic_flag
#define VAR_ATOMIC_FLAG_TESTSET(v) atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) atomic_flag_clear(&v)

#elif __cplusplus >= 201103L
#include <atomic>
#define VAR_ATOMIC(t) std::atomic<t>
#define VAR_ATOMIC_FLAG std::atomic_flag
#define VAR_ATOMIC_FLAG_TESTSET(v) std::atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) std::atomic_flag_clear(&v)

#else
/* the compiler will not create cpu memory barrier instructions,
 * this is not ideal but should not cause big problems. */
#define VAR_ATOMIC(t) volatile t
#define VAR_ATOMIC_FLAG volatile int
static FUN_INLINE int v_atomic_flag_test_and_set(volatile int *v)
{
    MEM_BARRIER();
    if (*v)
        return 1;
    *v = 1;
    MEM_BARRIER();
    return 0;
}
static FUN_INLINE void v_atomic_flag_clear(volatile int *v)
{
    MEM_BARRIER();
    *v = 0;
}

#define VAR_ATOMIC_FLAG_TESTSET(v) v_atomic_flag_test_and_set(&v)
#define VAR_ATOMIC_FLAG_CLEAR(v) v_atomic_flag_clear(&v)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pf_void_t)();
typedef void (*pf_checkassert_t)();

static struct resolve_state {
    VAR_ATOMIC(int) alldone;
    VAR_ATOMIC(int) state;
    VAR_ATOMIC_FLAG lock;

    pf_checkassert_t pf_checkassert;
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
        s_ResolveState.pf_checkassert = &noCheck;
        return 0;
    }
    pf = dlsym(RTLD_DEFAULT, PCHECKER_CHECKASSERT_NAME);
    if (pf) {
        COPY_PF(s_ResolveState.pf_checkassert, pf_checkassert_t, pf);
        return 1;
    }

    return 0;
}

static FUN_INLINE void callAssertFunction(int check)
{
    pf_checkassert_t pf = s_ResolveState.pf_checkassert;
    if (!check || pf)
        (*pf)();
}

#ifdef __cplusplus
}
#endif
