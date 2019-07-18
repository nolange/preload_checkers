#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pchecker_wrapper.h"
#include <stdlib.h>

#include <stdio.h>
#include <malloc.h>
#include <stdint.h>

#include <time.h>
#include <sys/time.h>

static void callback(void *p)
{
    (*(int *)p)++;
}

#define MEM_BARRIER()                          \
    do {                                       \
        __asm__ __volatile__("" ::: "memory"); \
    } while (0)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"

#define SIMPLE_TEST(n, ...)      \
    do {                         \
        uintptr_t v;             \
        printf("test " #n ": "); \
        MEM_BARRIER(); \
        count = 0; \
        v = (uintptr_t)n(__VA_ARGS__);          \
        MEM_BARRIER(); \
        randomvar ^= v; \
        printf("%d faults\n", count); \
    } while (0)

#pragma GCC diagnostic pop


int main()
{
    static volatile uintptr_t s_Sink;
    int count = 0;
    void *pMem;
    void *pToFree;
    uintptr_t randomvar; /* compilers do incredible stuff like eliminating unused pointers */


    const unsigned alignment = 16;
    const unsigned size = 16;

    randomvar = 0;

    /* this initialises the streams subsystem,
     * and this does various allocations
     * do this before enabling checkers */
    {
        volatile const char s_DontoptimizeMe[] = "starting...";
        volatile const char s_DontoptimizeMe2[] = "\n";

        printf("%s%s", s_DontoptimizeMe, s_DontoptimizeMe2);
    }

    printf("\nheap checker tests\n");
    set_cobalt_assert_nrt(&callback);
    enable_cobalt_assert_nrt_arg(1, 1, &count);

    pMem = NULL;

    printf("test " "malloc" ": ");
    MEM_BARRIER();
    count = 0;
    pToFree = malloc(size);
    MEM_BARRIER();

    printf("%d faults\n", count);
    randomvar ^= (uintptr_t)pToFree;

    SIMPLE_TEST(calloc, 1, size);

    SIMPLE_TEST(realloc, NULL, size);

    printf("test " "free" ": ");
    MEM_BARRIER();
    count = 0;
    free(pToFree);
    MEM_BARRIER();
    printf("%d faults\n", count);


    SIMPLE_TEST(posix_memalign, &pMem, alignment, size);

    SIMPLE_TEST(aligned_alloc,  alignment, size);

    SIMPLE_TEST(valloc, size);


    SIMPLE_TEST(memalign, alignment, size);
     /* SIMPLE_TEST(pvalloc, size); */


    printf("\ngettime checker tests\n");

    {
        struct timespec tp;
        struct timeval tv; struct timezone tz;
        SIMPLE_TEST(clock_gettime, CLOCK_MONOTONIC, &tp);

        SIMPLE_TEST(gettimeofday, &tv, &tz);

        SIMPLE_TEST(time, NULL);



        enable_cobalt_assert_nrt(0);
        s_Sink = randomvar;
    }
    (void)s_Sink;
    return 0;
}

int posix_memalign(void **memptr, size_t alignment, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
void *valloc(size_t size);

#include <malloc.h>

void *memalign(size_t alignment, size_t size);
void *pvalloc(size_t size);
