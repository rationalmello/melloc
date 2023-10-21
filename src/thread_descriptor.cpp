/**
 * @file thread_descriptor.cpp
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief ThreadDescriptor definitions
 * @version 1.0
 * @date 2023-09-14
 *
 *
 * Melloc::ThreadDescriptor definitions.
 * 
 * A ThreadDescriptor is created the first time a thread requests an 
 * allocation. It remains till the thread is terminated (todo: this.)
 *
 *
 *
 */

#include <atomic>
#include <cassert>
#include <iostream>
#include <signal.h>
#include <utility>

#include "atomic_guard.h"
#include "melloc.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


using BinCache = std::array<void*, THREAD_CACHE_SIZE>;

#ifdef __linux__
/*  Signal handler for timer which calls purge() */
void threadDescriptorSignalHandler(int sig, siginfo_t* si, void* uc) {
    Melloc::ThreadDescriptor* ptr = static_cast<Melloc::ThreadDescriptor*>(
        si->si_value.sival_ptr);
    ptr->purge();
}
#endif

/*  tid constructor */
Melloc::ThreadDescriptor::ThreadDescriptor(std::thread::id tid) 
    : tid(tid)
{
#ifdef __linux__
    sa.sa_sigaction = threadDescriptorSignalHandler;
    sa.sa_flags = SA_SIGINFO; /* allow passing this ptr thru siginfo_t */

    sev.sigev_value.sival_ptr = this;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMAX;

    myArena = getArena();
    for (std::atomic_flag& flg : usedFlags) {
        /* Initializing usedFlags {ATOMIC_FLAG_INIT} may not work for entire array */
        flg.clear();
    }
    /* arm decay timer */
    if (sigaction(SIGRTMAX, &sa, nullptr) == -1) {
        mellocPrint("sigaction bind failed");
        exit(1);
    }
    if (timer_create(CLOCK_REALTIME, &sev, &timerObj) == -1) {
        mellocPrint("timer creation failed");
        exit(1);
    }
    its.it_value.tv_sec = THREAD_PURGE_TIMER; /* time till first tick */
    its.it_interval.tv_sec = THREAD_PURGE_TIMER; /* repeated tick interval */
    if (timer_settime(timerObj, 0, &its, nullptr) == -1) {
        mellocPrint("timer arming failed");
        exit(1);
    }
#else 
    myArena = getArena();
    for (std::atomic_flag& flg : usedFlags) {
        /* Initializing usedFlags {ATOMIC_FLAG_INIT} may not work for entire array */
        flg.clear();
    }
#endif
}

/*  Pushes a chunk pointer onto thread's cache */
void Melloc::ThreadDescriptor::pushCache(void* ptr, std::size_t sizeClassIdx) noexcept {
    assert(sizeClassIdx >= 0);
    assert(sizeClassIdx < smallSizeClasses.size());
    AtomicFlagGuard g(usedFlags[sizeClassIdx]);
        
    decayRate[sizeClassIdx] = 1;
    std::size_t topIdx = topIdxs[sizeClassIdx];
    assert(topIdx >= 0);
    assert(topIdx <= THREAD_CACHE_SIZE);
    if (topIdx < THREAD_CACHE_SIZE) {
        ++topIdxs[sizeClassIdx];
        cache[sizeClassIdx][topIdx] = ptr;
        mellocPrint("inserted ptr 0x%x into threadDescriptor for sizeClass %zu",
            ptr, smallSizeClasses[sizeClassIdx]);
    }
    else {
        /* if no space, then return to bin immediately */
        arenas[myArena].bins[sizeClassIdx].giveBack(ptr);
    }
}

/*  Retrieves a chunk pointer from thread's cache. If cache is empty,
    returns nulltpr */
void* Melloc::ThreadDescriptor::popCache(std::size_t sizeClassIdx) noexcept {
    assert(sizeClassIdx >= 0);
    assert(sizeClassIdx < smallSizeClasses.size());
    AtomicFlagGuard g(usedFlags[sizeClassIdx]);

    decayRate[sizeClassIdx] = 1;
    std::size_t topIdx = topIdxs[sizeClassIdx];
    assert(topIdx >= 0);
    assert(topIdx <= THREAD_CACHE_SIZE);
    if (topIdx > 0) {
        --topIdxs[sizeClassIdx];
        return cache[sizeClassIdx][topIdx];
    }
    return nullptr;
}

/*  Exponentially decays existing buffer, slowly at first */
void Melloc::ThreadDescriptor::decay(std::size_t sizeClassIdx) noexcept {
    assert(sizeClassIdx > 0);
    topIdxs[sizeClassIdx] -= std::min(topIdxs[sizeClassIdx],
        decayRate[sizeClassIdx]);
    decayRate[sizeClassIdx] = std::max(decayRate[sizeClassIdx] << 1,
        THREAD_CACHE_SIZE);
}

/*  Do garbage collection for all size classes in specific thread  */
void Melloc::ThreadDescriptor::purge() {
    mellocPrint("purging thread 0x%x", this->tid);
    for (int i = 0; i < smallSizeClasses.size(); ++i) {
        AtomicFlagGuard g(usedFlags[i]);
        if (!topIdxs[i]) {
            continue;
        }
        std::size_t discards = std::min(decayRate[i], topIdxs[i]);
        assert(discards < THREAD_CACHE_SIZE);
        std::size_t topIdx = topIdxs[i]-1, botIdx = 1 + topIdx - discards;
        for ( ; topIdx > botIdx; --topIdx) {
            arenas[myArena].bins[i].giveBack(cache[i][topIdx]);
        }
        arenas[myArena].bins[i].giveBack(cache[i][botIdx]); /* edge case decrementing size_t past 0*/
        topIdxs[i] = botIdx;
        decayRate[i] = std::max(THREAD_CACHE_SIZE, decayRate[i] << 1);
    }
}