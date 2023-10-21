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

#ifndef UTIL_MELLOC_THREAD_DESCRIPTOR_H
#define UTIL_MELLOC_THREAD_DESCRIPTOR_H

#include <array>
#include "arena.h"
#include "melloc_defs.h"
#include "melloc.h"
#include "melloc_utils.h"

//#define sometimes
#ifdef sometimes


struct Melloc::Wrapper {
    int x;
    size_t get() {
        return arenas.size();
    }
};








#endif


#endif // UTIL_MELLOC_THREAD_DESCRIPTOR_H