/**
 * @file bin.cpp
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Bin definitions
 * @version 1.0
 * @date 2023-09-05
 *
 *
 * A Bin is a slab allocator responsible for all alloc and dealloc for a
 * small size class within an Arena. Calls to the Bin should only be made
 * after checking the ThreadDescriptor cache.
 * 
 */

#ifdef __linux__
#include <sys/mman.h>
#endif // __linux__

#include "melloc.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


void* Melloc::Arena::Bin::allocate() {
    std::unique_lock writeLock(mutBin, std::defer_lock);
    mellocPrint("allocation request on bin of sz %zu", smallSizeClasses[this->binIdx]);
    std::size_t sizeClass = smallSizeClasses[binIdx];
    void* out = nullptr;

    // first look through free list
    writeLock.lock();
    if (!binFreeChunks.empty()) {
        auto chunkIterator = binFreeChunks.begin();
        out = chunkIterator-> /* ptr */ first;
        if (--chunkIterator-> /* consecutive */ second > 0) {
            /* Change key of binFreeChunks without realloc using node handle */
            mellocPrint("decremented bin %zu chunk's consecutive, now becomes %zu ", sizeClass, chunkIterator->second);
            auto nh = binFreeChunks.extract(chunkIterator);
            nh.key() = increment(out, sizeClass);
            binFreeChunks.insert(std::move(nh));
        }
        else {
            mellocPrint("removed bin %zu chunk ", sizeClass);
            binFreeChunks.erase(chunkIterator);
        }
    }

    // otherwise, ask OS for slab (some contiguous pages)
    else {
        std::size_t objs = 0;
        std::size_t consecutive = 1;
        std::size_t sizeLim = PAGE_SIZE / MMAP_MIN_OBJECTS_TAKEN;
        std::size_t slab = PAGE_SIZE;
        if (sizeClass < sizeLim) {
            /* Get one page */
            objs = PAGE_SIZE / sizeClass;
        }
        else {
            slab = ((32 * sizeClass) & PAGE_MASK) + PAGE_SIZE * (isOffPage(32 * sizeClass));
            consecutive = slab / PAGE_SIZE;
            objs = slab / sizeClass;
        }
#ifdef __linux__
        out = mmap(/* preferred addr  */ nullptr,
                   /* size            */ slab,
                   /* protect flags   */ PROT_READ | PROT_WRITE,
                   /* map flags       */ MAP_PRIVATE | MAP_ANONYMOUS,
                   /* file descriptor */ 0,
                   /* chunk offset    */ 0);
#else
        out = malloc(slab);
#endif // __linux__
        objs = slab / sizeClass;
        mellocPrint("Bin sz %zu asked kernel for %zu bytes", sizeClass, slab);
        assert(getPage(out));
        
        std::unique_lock writeLockArena(arenas[myArena].mutArena);
        arenas[myArena].arenaUsedPages.emplace(getPage(out), binIdx, consecutive, true);
        writeLockArena.unlock();
        binFreeChunks.emplace(increment(out, sizeClass), objs - 1);
    }

    mellocPrint("returning ptr from bin: 0x%x", out);
    return out;
}


void Melloc::Arena::Bin::giveBack(void* ptr) {
    std::unique_lock writeLock(mutBin, std::defer_lock);
    std::size_t sizeClass = smallSizeClasses[binIdx];
    mellocPrint("giving back ptr 0x%x to sizeclass %zu", ptr, sizeClass);
    
    /* check if we can merge existing free entries */
    writeLock.lock();
    auto left = binFreeChunks.find(decrement(ptr, sizeClass));
    auto right = binFreeChunks.find(increment(ptr, sizeClass));
    if (left != binFreeChunks.end()) {
        ++left-> /* consecutive */ second;
    }
    if (right != binFreeChunks.end()) {
        if (left != binFreeChunks.end()) {
            left-> /* consecutive */ second += right-> /* consecutive */ second;
        }
        else {
            binFreeChunks.emplace(ptr, 1 + right-> /* consecutive */ second);
        }
        binFreeChunks.erase(right);
    }
}