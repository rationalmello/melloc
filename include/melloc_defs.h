/**
 * @file melloc_defs.h
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Definitions for parameters
 * @version 1.0
 * @date 2023-09-05
 *
 *
 * Definitions for parameters
 *
 *
 *
 */

#ifndef UTIL_MELLOC_DEFS_H
#define UTIL_MELLOC_DEFS_H

#include <array>
#include <cassert>
#ifndef NDEBUG
#include <iostream>
#endif // NDEBUG

/*   Using 4096 because easier to work with. Depends on system and whether
     Transparent Huge Pages enabled, but stubbed for now */
#define PAGE_SIZE               (4096U)

/*   Corresponding bitmask for getting the page number to above page size */
#define PAGE_MASK               (0xFFFFF000)

/*   Number of arenas. Jemalloc uses 4 x number of CPU cores. Stubbed for now */
#define NUM_ARENAS              (1)

 /*  Number of max cached items per size class per thread. Larger thread cache 
     will have less peak lock contention, but more peak metadata memory */
#define THREAD_CACHE_SIZE       (static_cast<std::size_t>(16))

/*   Minimum number of objects requested per size class when a bin runs out of
     memory. */
#define MMAP_MIN_OBJECTS_TAKEN  (32)

/*   Number of seconds between every call for per-thread garbage collector */
#define THREAD_PURGE_TIMER      (2)


static_assert(PAGE_SIZE > 0);
static_assert(THREAD_CACHE_SIZE > 0);
static_assert(PAGE_SIZE > 0);

static constexpr std::array<std::size_t, 28> smallSizeClasses{
    /* 0*/  8,
    /* 1*/  16,
    /* 2*/  32,
    /* 3*/  48,
    /* 4*/  64,
    /* 5*/  80,
    /* 6*/  96,
    /* 7*/  112,
    /* 8*/  128,
    /* 9*/  192,
    /* 10*/ 256,
    /* 11*/ 320,
    /* 12*/ 384,
    /* 13*/ 448,
    /* 14*/ 512,
    /* 15*/ 768,
    /* 16*/ 1024,
    /* 17*/ 1280,
    /* 18*/ 1536,
    /* 19*/ 1792,
    /* 20*/ 2048,
    /* 21*/ 2304,
    /* 22*/ 2560,
    /* 23*/ 2816,
    /* 24*/ 3072,
    /* 25*/ 3328,
    /* 26*/ 3584,
    /* 27*/ 3840
};


#endif // UTIL_MELLOC_DEFS_H