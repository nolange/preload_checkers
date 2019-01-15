/*
 * this checker interposes on the family of malloc/free functions,
 * which adds some complications as dlsym will call those functions.
 * To make these first allocations succeed, its necessary to provide an
 * initial buffer and heap operations.
 *
 * if a second operation is detected, then the initial buffer is used.
 * after that, the dlsym methods should be usable and the real functions
 * will replace the initial stubs
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

struct max_align_type {
    long integral[32 / sizeof(long)];
    double fpoint[32 / sizeof(double)];
    void *paddress[32 / sizeof(void *)];
};

static struct static_heap_res {
    struct max_align_type rawbuffer[3 * 1024 / sizeof(struct max_align_type)];

    VAR_ATOMIC(unsigned) offset;

    double alignme;
} s_StaticHeap;

static FUN_INLINE unsigned ALIGN(unsigned v, unsigned a)
{
    return (v + a) & ~a;
}

static FUN_INLINE int isPow2OrZero(size_t v)
{
    return (v & ~v) == v;
}

static FUN_INLINE unsigned checkStaticBufferAlloc(void *ptr)
{
#ifdef __UINTPTR_TYPE__
    typedef __UINTPTR_TYPE__ ptr_t;
#else
    typedef size_t ptr_t;
#endif
    return (ptr_t)((char *)ptr - (char *)s_StaticHeap.rawbuffer) < sizeof(s_StaticHeap.rawbuffer);
}

static FUN_INLINE unsigned loadStaticOffset()
{
    unsigned r = s_StaticHeap.offset;
    MEM_BARRIER();
    return r;
}

static FUN_INLINE int exchangeStaticOffset(unsigned *pE, unsigned n)
{
#if __cplusplus >= 201103L
    return std::atomic_compare_exchange_weak<unsigned>(&s_StaticHeap.offset, pE, n);
#elif __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
    return atomic_compare_exchange_weak(&s_StaticHeap.offset, pE, n);
#else
    unsigned old;

    /* the compiler will not create memory barrier instructions,
     * this is not ideal but should not cause big problems. */
    MEM_BARRIER();
    old = s_StaticHeap.offset;
    if (old == *pE) {
        s_StaticHeap.offset = n;
        MEM_BARRIER();
        return 1;
    }
    return 0;

#endif
}

/*
 * Static allocator to use when initially executing dlsym(). It keeps a
 * size_t value of each object size prior to the object.
 */
static void *static_calloc_aligned(size_t size, unsigned alignment)
{
    typedef struct max_align_type raw_t;
    unsigned new_offset, res_offset, aligned_offset;

    if (size == 0) {
        return NULL;
    }

    alignment = alignment > sizeof(raw_t) ? alignment : sizeof(raw_t);

    res_offset = loadStaticOffset();
    do {
        aligned_offset = ALIGN(res_offset + sizeof(unsigned), alignment);
        new_offset = aligned_offset + size;
        if (size >= sizeof(s_StaticHeap.rawbuffer) || new_offset > sizeof(s_StaticHeap.rawbuffer)) {
            return NULL;
        }
    } while (!exchangeStaticOffset(&res_offset, new_offset));

    FUN_MEMCPY((unsigned *)(s_StaticHeap.rawbuffer + aligned_offset) - 1, &size, sizeof(size));
    return s_StaticHeap.rawbuffer + aligned_offset;
}

static void *static_calloc(size_t nmemb, size_t size)
{
    return static_calloc_aligned(nmemb * size, 0);
}

static void *static_malloc(size_t size)
{
    return static_calloc_aligned(size, 0);
}

static FUN_INLINE void static_free(void *ptr)
{
    (void)ptr;
}

/* small memcpy for the realloc functions,
 * those are typically the only callsites of the real memcpy */

static void small_memcpy(void *_vdst, const void *_vsrc, size_t len)
{
    char *dst = (char *)_vdst;
    const char *src = (const char *)_vsrc;

    while (len--)
        *dst++ = *src++;
}

DSO_HIDDEN void *memcpy(void *_vdst, const void *_vsrc, size_t len)
{
    small_memcpy(_vdst, _vsrc, len);
    return _vdst;
}

static void *static_realloc(void *ptr, size_t size)
{
    unsigned old_size;
    void *newptr;
    if (size == 0) {
        return NULL;
    }

    if (ptr) {
        FUN_MEMCPY(&old_size, (unsigned *)ptr - 1, sizeof(old_size));

        if (size <= old_size) {
            /* We can re-use the old entry. */
            FUN_MEMCPY((unsigned *)ptr - 1, &old_size, sizeof(old_size));
            return ptr;
        }
    }

    /* We need to expand. Don't free previous memory location. */
    newptr = static_calloc_aligned(size, 0);
    if (newptr) {

        if (ptr)
            small_memcpy(newptr, ptr, old_size);
    }

    return newptr;
}

static void *static_reallocarray(void *ptr, size_t nmemb, size_t size)
{
    return static_realloc(ptr, nmemb * size);
}

/* The function aligned_alloc() is the same as memalign(), except for
 * the added restriction that size should be a multiple of alignment.
 */
static void *static_aligned_alloc(size_t alignment, size_t size)
{
    return static_calloc_aligned(size, alignment);
}

/* The obsolete function memalign() allocates size bytes and returns a
 * pointer to the allocated memory.  The memory address will be a
 * multiple of alignment, which must be a power of two.
 * OBSOLETE!
 */
#define static_memalign static_aligned_alloc

/* The function posix_memalign() allocates size bytes and places the
 * address of the allocated memory in *memptr.  The address of the
 * allocated memory will be a multiple of alignment, which must be a
 * power of two and a multiple of sizeof(void *).
 */
static int static_posix_memalign(void **memptr, size_t alignment, size_t size)
{
    void *ptr;
#ifdef ENOMEM
    const int enomem = ENOMEM;
#else
    const int enomem = 12; /* Out of memory */
#endif

    if (!isPow2OrZero(alignment) || (alignment % sizeof(void *))) {
        return EINVAL;
    }
    ptr = static_calloc_aligned(size, alignment);
    if (unlikely(!ptr) && size)
        return enomem;
    *memptr = ptr;

    return 0;
}

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

static FUN_INLINE void initTable()
{
    getassert_function(0);
}

static FUN_INLINE const char *getSymbolName(enum EFunctionIndex func)
{
    const char *pName = s_FunctionNames;
    int index;

    for (index = 0; *pName != '\0' && index < (int)func; ++index) {
        while (*pName++ != '\0')
            ;
    }
    return *pName != '\0' ? pName : NULL;
}

static int tryResolve(enum EFunctionIndex func)
{
    int state;

    /* avoid deadlock */
    if (!acquireLock())
        return -128;

    state = setState(0);

    if (state == 0) {
        initTable();
        state = setState(1);
    }

    if (state <= 1) {
        /* resolve the 4 elementary functions calloc, malloc, free and realloc
         * any symbol that is missing will cause allocation and a crash if
         * those functions are not already available */
        unsigned index;
        int countresolved = 0;
        const char *pName = s_FunctionNames;

        struct function_table newfTable = {NULL};
        pf_void_t *pFTable = (pf_void_t *)&newfTable.pf_calloc;

        for (index = 0; index < eLastBaseFunction; ++index) {
            void *pf;
            pf = getdelegate_function(pName);
            if (pf)
                FUN_MEMCPY(pFTable, &pf, sizeof(*pFTable));

            countresolved += pf ? 1 : 0;

            while (*pName++ != '\0')
                ;
            ++pFTable;
        }

        if (countresolved == eLastBaseFunction) {
            FUN_MEMCPY(&s_ResolvedFunctions, &newfTable, sizeof(pFTable) * eLastBaseFunction);
            state = setState(2);
        }
    }

    if (state == 2) {
        /* resolve all delegate functions */

        int countresolved = 0;
        const char *pName = getSymbolName(func);

        struct function_table newfTable = {NULL};
        pf_void_t *pFTable = (pf_void_t *)&newfTable.pf_calloc;

        {
            /* quite possibly we could be smarter, try
             * resolving only the symbols used by libdl first.
             * this should limit the recursive calls to a minimum */

            void *pf;
            pf = getdelegate_function(pName);
            if (pf)
                FUN_MEMCPY((pf_void_t *)&s_ResolvedFunctions.pf_calloc + (int)func, &pf, sizeof(pf_void_t));
        }

        pName = getSymbolName((enum EFunctionIndex)0);

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

#define DO_INIT_FOR_FUNCTION(e, n, pf, ps)                            \
    do {                                                              \
        (pf) = s_ResolvedFunctions.pf_##n;                            \
        if (unlikely(!initIsDone() || !(pf))) {                       \
            int state = tryResolve(e);                                \
            (pf) = s_ResolvedFunctions.pf_##n;                        \
            if (!(pf)) {                                              \
                if (state <= -128) {                                  \
                    if ((ps) != NULL)                                 \
                        *(int *)(ps) = 1;                             \
                    (pf) = static_##n; /* we are in recursive call */ \
                    break;             /* skip calling assert */      \
                }                                                     \
                else                                                  \
                    do_abort(); /* This function does not exist */    \
            }                                                         \
        }                                                             \
        callAssertFunction(1);                                        \
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
    pf_calloc_t pf;
    DO_INIT_FOR_FUNCTION(eCalloc, calloc, pf, NULL);

    return (*pf)(nmemb, size);
}
void *malloc(size_t size)
{
    pf_malloc_t pf;
    DO_INIT_FOR_FUNCTION(eMalloc, malloc, pf, NULL);

    return (*pf)(size);
}
void free(void *ptr)
{
    pf_free_t pf;
    DO_INIT_FOR_FUNCTION(eFree, free, pf, NULL);

    if (unlikely(checkStaticBufferAlloc(ptr)))
        static_free(ptr);
    else
        (*pf)(ptr);
}
void *realloc(void *ptr, size_t size)
{
    pf_realloc_t pf;
    int isStatic = 0;
    DO_INIT_FOR_FUNCTION(eRealloc, realloc, pf, &isStatic);

    if (unlikely(checkStaticBufferAlloc(ptr)) && !isStatic) {
        void *newPtr = (*pf)(NULL, size);

        if (newPtr) {
            /* copy from static buffer */
            size_t old_size;
            FUN_MEMCPY(&old_size, (size_t *)ptr - 1, sizeof(old_size));

            small_memcpy(newPtr, ptr, old_size);
        }

        return newPtr;
    }

    return (*pf)(ptr, size);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
    pf_reallocarray_t pf;
    int isStatic = 0;
    DO_INIT_FOR_FUNCTION(eReallocArray, reallocarray, pf, &isStatic);

    return (*pf)(ptr, nmemb, size);
}

void *memalign(size_t alignment, size_t size)
{
    pf_memalign_t pf;
    DO_INIT_FOR_FUNCTION(eMemalign, memalign, pf, NULL);

    return (*pf)(alignment, size);
}
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    pf_posix_memalign_t pf;
    DO_INIT_FOR_FUNCTION(ePosixMemalign, posix_memalign, pf, NULL);

    return (*pf)(memptr, alignment, size);
}
void *aligned_alloc(size_t alignment, size_t size)
{
    pf_aligned_alloc_t pf;
    DO_INIT_FOR_FUNCTION(eAlignedAlloc, aligned_alloc, pf, NULL);

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
