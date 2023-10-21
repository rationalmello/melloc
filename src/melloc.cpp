/**
 * @file melloc.cpp
 * 
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Custom memory allocator
 * @version 1.0
 * @date 2023-09-05
 * 
 * 
 * Definitions for Melloc members
 * 
 *
 */


#include <cassert>
#include <shared_mutex>

#include "melloc.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


#ifdef __linux__
#else
void* sbrk(size_t bytes) {
    /* stub for non-linux builds */
    return static_cast<void*>(malloc(bytes));
}
#endif


/* Constructor */
Melloc::Melloc() {init();};

/* Allocate memory */
[[nodiscard]]
void* Melloc::allocate(std::size_t n) {
    std::shared_lock readLock(mutMelloc);
    std::size_t sz = roundup(n);
    std::thread::id tid = std::this_thread::get_id();
    
    auto threadDescriptorIt = threadDescriptors.find(tid);
    if (threadDescriptorIt == threadDescriptors.end()) {
        /*  First allocation for this thread, assign via round-robin.
            Initialize thread cache as well */
        readLock.unlock();
        std::unique_lock writeLock(mutMelloc);
        threadDescriptorIt = threadDescriptors.emplace(
            tid, tid /* ThreadDescriptorWrapper(tid) */).first;
        writeLock.unlock();
        readLock.lock();
    }
    assert(threadDescriptorIt != threadDescriptors.end());
    Arena& arena = arenas[threadDescriptorIt->second->myArena];
    
    return arena.allocate(sz, threadDescriptorIt->second);
}

/*  Free memory. Caller is responsible for ensuring the address is valid (ie.
    has previously been returned by allocate()), else undefined behavior, 
    like in malloc */
void Melloc::deallocate(void* ptr) noexcept {
    std::shared_lock readLock(mutMelloc);
    auto threadDescriptorIt = threadDescriptors.find(std::this_thread::get_id());
    assert(threadDescriptorIt != threadDescriptors.end());
    Arena& arena = arenas[threadDescriptorIt->second->myArena];
    arena.deallocate(ptr);
}

/* Assign arena */
[[nodiscard]]
size_t Melloc::getArena() noexcept {
    return 0; // todo: change to rolling counter
}

/*  return the bin index corresponding to a particular size. */
std::size_t Melloc::getBinIdx(std::size_t sz) noexcept {
    std::size_t bin = 0;
    if (sz <= 8) {
        return 0;
    }
    if (sz <= 16) {
        return 1;
    }
    if (sz < 192) {
        bin = 1 + ((sz-16) >> 4); // chunks by 16
        return bin + !(smallSizeClasses[bin] == sz);
    }
    if (sz < 768) {
        bin = 9 + ((sz-192) >> 6); // chunks by 64
        return bin + !(smallSizeClasses[bin] == sz);
    }
    else {
        bin = 15 + ((sz-768) >> 8); // chunks by 256
        return bin + !(smallSizeClasses[bin] == sz);
    }
}

/*  round up to nearest small or large size class. */
std::size_t Melloc::roundup(std::size_t sz) noexcept {
    assert(sz >= 0);
    if (isLargeSize(sz)) {
        return (sz + PAGE_SIZE*isOffPage(sz)) & PAGE_MASK;
    }
    return smallSizeClasses[getBinIdx(sz)];
}

/*  We only want one instance of Melloc per virtual address space, but we cannot make
    Melloc members static, else we are forced to either pollute main file with declarations
    or have undefined global var initialization order (segfault on accessing bad ptr) */
void Melloc::init() noexcept {
    if (globalInit) {
        exit(1);
    }
    globalInit = true;
}


// Melloc static members
#ifndef NDEBUG
std::mutex                                                  Melloc::mutPrint;
#endif // NDEBUG
std::shared_mutex                                           Melloc::mutMelloc; 
std::array<Melloc::Arena, NUM_ARENAS>                       Melloc::arenas;
std::unordered_map<std::thread::id,
                   Melloc::ThreadDescriptorWrapper,
                   Melloc::ThreadDescriptorWrapper::hash>   Melloc::threadDescriptors;

