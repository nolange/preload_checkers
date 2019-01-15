/*
 * compile with ${PRE}gcc -g2 -O2 -std=c11 -Wall -Wextra -pedantic ${SRC}libcobalt-gettime_checker.c -ldl -fPIC -shared
 * -o libcobalt-gettime_checker.so -fno-plt -Wl,--enable-new-dtags,-z,relro,-z,now -Wl,-as-needed
 *
 * glibc might use __vdso_clock_gettime, __vdso_gettimeofday, __vdso_time.
 * but there ir no getcpu in glibc to interpose
 *
 * http://man7.org/linux/man-pages/man7/vdso.7.html
 */

#include "pchecker.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timespec;
struct timeval;
struct timezone;

typedef int (*pf_clock_gettime_t)(clockid_t clock_id, struct timespec *tp);
typedef int (*pf_gettimeofday_t)(struct timeval *tv, struct timezone *tz);
typedef time_t (*pf_time_t)(time_t *t);

DSO_PUBLIC int clock_gettime(clockid_t clock_id, struct timespec *tp);
DSO_PUBLIC int gettimeofday(struct timeval *tv, struct timezone *tz);
DSO_PUBLIC time_t time(time_t *t);

static int no_clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    (void)clock_id;
    (void)tp;
    if (resolveIsDone())
        FUN_TRAP();
    return EINVAL;
}

static int no_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tv;
    (void)tz;
    if (resolveIsDone())
        FUN_TRAP();
    return EINVAL;
}

static time_t no_time(time_t *t)
{
    (void)t;
    if (resolveIsDone())
        FUN_TRAP();
    return -1;
}

static struct function_table {
    pf_clock_gettime_t pf_clock_gettime; /* librt */

    pf_gettimeofday_t pf_gettimeofday; /* libc */
    pf_time_t pf_time;                 /* libc */
} s_ResolvedFunctions;

/* This wrapper does not pull in all dependencies except libdl and
 * indirectly libc,
 * so functions from other libraries might not be available yet
 * (outside of libcobalt that would be a missing dependency at the call-site).
 *
 * We handle this by setting fallbacks early.
 *
 * (constructors don't seem to be called earlier than dependent DSO,
 * this did not work!) */

static FUN_INLINE void initTable()
{
    getassert_function(0);

    s_ResolvedFunctions.pf_clock_gettime = &no_clock_gettime;
    s_ResolvedFunctions.pf_gettimeofday = &no_gettimeofday;
    s_ResolvedFunctions.pf_time = &no_time;
}

/* clang-format off */
static const char *const s_FunctionNames =
    "clock_gettime\0"
    "gettimeofday\0"
    "time\0";
/* clang-format on */

static int tryResolve()
{
    int state = setState(0);

    if (state == 0) {
        initTable();
        state = setState(1);
    }

    if (state <= 2) {
        /* resolve all delegate functions */

        int countresolved = 0;
        const char *pName = s_FunctionNames;

        pf_void_t *pFTable = (pf_void_t *)&s_ResolvedFunctions.pf_clock_gettime;

        while (*pName != '\0') {
            void *pf;
            pf = getdelegate_function(pName);
            if (pf)
                FUN_MEMCPY(pFTable, &pf, sizeof(*pFTable));

            countresolved += pf ? 1 : 0;

            while (*pName++ != '\0')
                ;
            ++pFTable;
        }

        if (countresolved == sizeof(s_ResolvedFunctions) / sizeof(pf_void_t))
            state = setState(3);
    }

    /* libcobalt should appear after regular linux libs
     * if we find the assert function consider symbol resolving
     * completely done */
    if (state >= 2) {
        if (getassert_function(1) && state == 3)
            state = setResolveIsDone();
    }

    return state;
}

__attribute__((__constructor__(101))) static void callResolve()
{
    /* ensure the resolve function gets called,
     * hopefully before threads are spawned */
    if (!initIsDone())
        tryResolve();
    /* DSOs should all be loaded at this point,
     * so don't try again */
    setInitIsDone();
}

static FUN_INLINE void initAndCheck()
{
    if (unlikely(!initIsDone())) {
        tryResolve();
    }

    callAssertFunction(0);
}

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    initAndCheck();

    return (*s_ResolvedFunctions.pf_clock_gettime)(clock_id, tp);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    initAndCheck();

    return (*s_ResolvedFunctions.pf_gettimeofday)(tv, tz);
}

time_t time(time_t *t)
{
    initAndCheck();

    return (*s_ResolvedFunctions.pf_time)(t);
}

#ifdef __cplusplus
}
#endif
