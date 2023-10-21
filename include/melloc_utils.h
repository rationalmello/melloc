/**
 * @file melloc_utils.h
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Utility helper functions
 * @version 1.0
 * @date 2023-10-06
 *
 * 
 * Utility helper functions
 *
 */


#ifndef UTIL_MELLOC_UTILS_H
#define UTIL_MELLOC_UTILS_H


#include <cstddef>
#ifndef NDEBUG
#include <cstdio>
#include <mutex>
#endif // NDEBUG
#include "melloc_defs.h"


/*  Debug macro for printf, I couldn't get it working with cout */
#ifndef NDEBUG
#define mellocPrint(fmt, ...) \
        do { std::unique_lock writeLock(Melloc::mutPrint); \
             std::printf("%s: Line %d: %s():\n    ", \
                         __FILE__, __LINE__, __func__); \
             std::printf(fmt, ##__VA_ARGS__); \
             std::printf("\n"); \
        } while (0)
#else
#define mellocPrint(fmt, ...) ((void)0)
#endif // NDEBUG


using pointer = void*;
using Page = uint64_t;

inline Page getPage(void* addr) noexcept {
    assert(addr != nullptr);
    return (reinterpret_cast<Page>(addr) & PAGE_MASK);
}

inline bool isLargeSize(std::size_t sz) noexcept {
    return sz >= PAGE_SIZE;
}

inline bool isOffPage(std::size_t sz) noexcept {
    return !(sz == (sz & PAGE_MASK));
}

/*  Pointer address arithmetic */
inline void* increment(void* ptr, std::size_t sz) noexcept {
    return static_cast<char*>(ptr) + sz;
}

/*  Pointer address arithmetic */
inline void* decrement(void* ptr, std::size_t sz) noexcept {
    return static_cast<char*>(ptr) - sz;
}


#endif // UTIL_MELLOC_UTILS_H