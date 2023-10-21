/**
 * @file
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief
 * @version 1.0
 * @date 2023-09-05
 *
 *
 *
 *
 *
 *
 */

#ifdef never

//#include "internalDenseAlloc.h"
#include <array>
#include <thread>
#include <utility>
#include <memory>
#include <iostream>
#include <mutex>
#include <set>
#include <map>
#include <unordered_map>


/*	Store metadata nodes densely packed (red-black tree nodes, etc.) */
template <typename T>
struct InternalDenseAlloc {
    using value_type = T;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using void_pointer = void*;
    using size_type = size_t; // optional: todo
    using difference_type = size_t; // optional: todo
    using Page = uint64_t;

    /* Allocate memory */
    [[nodiscard]]
    pointer allocate(size_type n) {

    }

    /*  Free memory. Caller is responsible for ensuring the address is valid (ie.
    has previously been returned by allocate()), else undefined behavior,
    like in malloc() interface standard. */
    void deallocate(pointer ptr, size_t) noexcept {

    }

    // InternalDenseAlloc members
    static void* nextSlab;
    static 
};

#endif