/*
 * this checker interposes on the family of malloc/free functions,
 * which adds some complications as dlsym will call those functions.
 *
 * It assumes that the dlsym function only uses the standard C functions,
 * and if necessary can break recursion by using aliased functions
 * specific to glibc.
 */

#include "pchecker.h"

#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static FUN_INLINE void do_abort()
{
    FUN_TRAP();
}

typedef void *(*pf_calloc_t)(size_t nmemb, size_t size);
typedef void *(*pf_malloc_t)(size_t size);
typedef void (*pf_free_t)(void *ptr);
typedef void *(*pf_realloc_t)(void *ptr, size_t size);
typedef void *(*pf_reallocarray_t)(void *ptr, size_t nmemb, size_t size);
typedef void *(*pf_memalign_t)(size_t alignment, size_t size);
typedef void *(*pf_aligned_alloc_t)(size_t alignment, size_t size);
typedef int (*pf_posix_memalign_t)(void **memptr, size_t alignment, size_t size);
typedef void *(*pf_valloc_t)(size_t size);
typedef void *(*pf_pvalloc_t)(size_t size);

DSO_PUBLIC void *calloc(size_t nmemb, size_t size);
DSO_PUBLIC void *malloc(size_t size);
DSO_PUBLIC void free(void *ptr);
DSO_PUBLIC void *realloc(void *ptr, size_t size);
DSO_PUBLIC void *reallocarray(void *ptr, size_t nmemb, size_t size);
DSO_PUBLIC void *memalign(size_t alignment, size_t size);
DSO_PUBLIC void *aligned_alloc(size_t alignment, size_t size);
DSO_PUBLIC int posix_memalign(void **memptr, size_t alignment, size_t size);
DSO_PUBLIC void *aligned_alloc(size_t alignment, size_t size);
DSO_PUBLIC void *valloc(size_t size);
DSO_PUBLIC void *pvalloc(size_t size);

/* glibc makes the dynamic resolution hard, but exports the functions with other names */
DSO_PUBLIC void *__libc_calloc(size_t nmemb, size_t size);
DSO_PUBLIC void *__libc_malloc(size_t size);
DSO_PUBLIC void __libc_free(void *ptr);
DSO_PUBLIC void *__libc_realloc(void *ptr, size_t size);

static struct function_table {
    pf_calloc_t pf_calloc;
    pf_malloc_t pf_malloc;
    pf_free_t pf_free;
    pf_realloc_t pf_realloc;
    pf_reallocarray_t pf_reallocarray;
    pf_memalign_t pf_memalign;
    pf_aligned_alloc_t pf_aligned_alloc;
    pf_posix_memalign_t pf_posix_memalign;
    pf_valloc_t pf_valloc;
    pf_pvalloc_t pf_pvalloc;
} s_ResolvedFunctions;

enum EFunctionIndex {
    eCalloc,
    eMalloc,
    eFree,
    eRealloc,
    eReallocArray,
    eMemalign,
    eAlignedAlloc,
    ePosixMemalign,
    eValloc,
    ePValloc,

    eLastBaseFunction = eRealloc
};

/* clang-format off */
static const char *const s_FunctionNames =
    "calloc\0"
    "malloc\0"
    "free\0"
    "realloc\0"
    "reallocarray\0"
    "memalign\0"
    "aligned_alloc\0"
    "posix_memalign\0"
    "valloc\0"
    "pvalloc\0";
/* clang-format on */

/* This wrapper does not pull in all dependencies except libdl and
 * indirectly libc,
 * so functions from other libraries might not be available yet
 * (outside of libcobalt that would be a missing dependency at the call-site).
 *
 * We handle this by setting fallbacks early.
 *
 * (constructors don't seem to be called earlier than dependent DSO,
 * this did not work!) */

static FUN_INLINE void initTable() {}

static int tryResolve(enum EFunctionIndex func)
{
    int state;
    (void)func;

    /* avoid deadlock */
    if (!acquireLock())
        return -128;

    state = setState(0);

    if (state == 0) {
        initTable();
        state = setState(1);
    }

    if (state <= 2) {
        /* resolve all delegate functions */

        int countresolved = 0;
        const char *pName = s_FunctionNames;

        struct function_table newfTable = {NULL};
        pf_void_t *pFTable = (pf_void_t *)&newfTable.pf_calloc;

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

        if (countresolved > eLastBaseFunction) {
            FUN_MEMCPY(&s_ResolvedFunctions, &newfTable, sizeof(newfTable));
            state = setState(3);
        }
    }

    /* libcobalt should appear after regular linux libs
     * if we find the assert function consider symbol resolving
     * completely done */
    if (state >= 2) {
        if (getassert_function(1) && state == 3)
            state = setResolveIsDone();
    }

    releaseLock();
    return state;
}

static FUN_INLINE void initAndCheck(enum EFunctionIndex func)
{
    if (unlikely(!initIsDone())) {
        tryResolve(func);
    }

    callAssertFunction(1);
}

__attribute__((__constructor__(101))) static void callResolve()
{
    /* ensure the resolve function gets called,
     * hopefully before threads are spawned */
    if (!initIsDone())
        tryResolve((enum EFunctionIndex)0);
    /* DSOs should all be loaded at this point,
     * so don't try again */
    setInitIsDone();
}

#define DO_INIT_FOR_GLIBC_FUNCTION(e, n)                        \
    pf_##n##_t pf = s_ResolvedFunctions.pf_##n;                 \
    do {                                                        \
        if (unlikely(!initIsDone() || !pf)) {                   \
            int state = tryResolve(e);                          \
            if (state <= -128) {                                \
                pf = __libc_##n; /* we are in recursive call */ \
                break;                                          \
            }                                                   \
            else                                                \
                pf = s_ResolvedFunctions.pf_##n;                \
        }                                                       \
        callAssertFunction(1);                                  \
    } while (0)

#define DO_INIT_NO_FALLBACK(e, n)               \
    pf_##n##_t pf = s_ResolvedFunctions.pf_##n; \
    do {                                        \
        int isInitDone = initIsDone();          \
        if (unlikely(!isInitDone || !pf)) {     \
            if (!isInitDone)                    \
                tryResolve(e);                  \
            pf = s_ResolvedFunctions.pf_##n;    \
            if (!pf)                            \
                do_abort();                     \
        }                                       \
        callAssertFunction(1);                  \
    } while (0)

void *calloc(size_t nmemb, size_t size)
{
    DO_INIT_FOR_GLIBC_FUNCTION(eCalloc, calloc);

    return (*pf)(nmemb, size);
}
void *malloc(size_t size)
{
    DO_INIT_FOR_GLIBC_FUNCTION(eMalloc, malloc);

    return (*pf)(size);
}
void free(void *ptr)
{
    DO_INIT_FOR_GLIBC_FUNCTION(eFree, free);

    (*pf)(ptr);
}
void *realloc(void *ptr, size_t size)
{
    DO_INIT_FOR_GLIBC_FUNCTION(eRealloc, realloc);

    return (*pf)(ptr, size);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    DO_INIT_NO_FALLBACK(eReallocArray, reallocarray);

    return (*pf)(ptr, nmemb, size);
}
void *memalign(size_t alignment, size_t size)
{
    DO_INIT_NO_FALLBACK(eMemalign, memalign);

    return (*pf)(alignment, size);
}
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    DO_INIT_NO_FALLBACK(ePosixMemalign, posix_memalign);

    return (*pf)(memptr, alignment, size);
}
void *aligned_alloc(size_t alignment, size_t size)
{
    DO_INIT_NO_FALLBACK(eAlignedAlloc, aligned_alloc);

    return (*pf)(alignment, size);
}
/* No static fallbacks for the remaining functions */
void *valloc(size_t size)
{
    DO_INIT_NO_FALLBACK(eValloc, valloc);

    return (*pf)(size);
}
void *pvalloc(size_t size)
{
    DO_INIT_NO_FALLBACK(ePValloc, pvalloc);

    return (*pf)(size);
}

#ifdef __cplusplus
}
#endif
