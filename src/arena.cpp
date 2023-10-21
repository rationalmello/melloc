/**
 * @file arena.cpp
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Arena definitions
 * @version 1.0
 * @date 2023-09-05
 *
 *
 * An arena can be considered a sub-heap
 * All arenas are initialized when a new virtual address space is created.
 *                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
 */

#include <cassert>
#ifdef __linux__
#include <sys/mman.h>
#endif // __linux__

#include "melloc.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


Melloc::Arena::Arena() {
    init();
}

[[nodiscard]]
void* Melloc::Arena::allocate(std::size_t sz,
    Melloc::ThreadDescriptorWrapper& tdw) {
    if (isLargeSize(sz)) {
        /*  Need to map large objects here in arena, since they don't 
            belong to a bin */
        std::unique_lock writeLock(mutArena, std::defer_lock);
#ifdef __linux__
        pointer out = static_cast<pointer>(
            mmap(/* preferred addr  */ nullptr,
                 /* size            */ sz,
                 /* protect flags   */ PROT_READ | PROT_WRITE,
                 /* map flags       */ MAP_PRIVATE | MAP_ANONYMOUS,
                 /* file descriptor */ 0,
                 /* chunk offset    */ 0));
        if (out == MAP_FAILED) {
            exit(1);
        }

        writeLock.lock();
        arenaUsedPages.emplace(getPage(out), sz, false);
        mellocPrint("large object of size %zu mapped to 0x%x", sz, out);
        return out;
#else    
        writeLock.lock();
        void* out = malloc(sz);
        arenaUsedPages.emplace(getPage(out), sz, false);
        mellocPrint("large object of size %zu alloc'd to ptr 0x%x", sz, out);
        return out;
#endif // __linux__
    }

    /* Small objects are thread cacheable */
    std::size_t binIdx = getBinIdx(sz);
    void* out = tdw->popCache(binIdx);
    if (out) {
        return out;
    }

    /* Get small object from bin  */
    return bins[binIdx].allocate();
}

void Melloc::Arena::deallocate(void* ptr) noexcept {
    std::shared_lock readLock(mutArena);
    auto pageIt = arenaUsedPages.lower_bound(getPage(ptr));
    if (pageIt == arenaUsedPages.end() ||
        (pageIt->isSlab &&
         pageIt->page + pageIt->sizeInfo.slab.consecutive * PAGE_SIZE <= getPage(ptr))) {
        /*  todo: freeing from a thread other than the original assignee is
            stubbed for now. But we shouldn't be doing this anyways */
        mellocPrint("bad to free from non-assignee thread");
        exit(1);
        return;
    }

    /*  Large chunks are not thread cacheable */
    if (!pageIt->isSlab) {
#ifdef __linux__
        if (munmap(ptr, pageIt->sizeInfo.len) == -1) {
            exit(1);
        }
        mellocPrint("unmapped large object at 0x%x", ptr);
#else
        free(ptr);
#endif // __linux
        return;
    }

    /*  Small or medium chunks are thread cacheable */
    std::size_t binIdx = pageIt->sizeInfo.slab.binIdx;
    std::thread::id tid = std::this_thread::get_id();
    auto threadDescriptorIt = threadDescriptors.find(tid);
    assert(threadDescriptorIt != threadDescriptors.end());

    Melloc::ThreadDescriptorWrapper& tdw = threadDescriptorIt->second;
    tdw->pushCache(ptr, binIdx);
}

void Melloc::Arena::init() {
    /*  Populate all bins */
    assert(bins.size() > 0);
    for (int i = 0; i < bins.size(); ++i) {
        Bin& b = bins[i];
        b.myArena = id;
        b.binIdx = i;
        void* out = nullptr;
        std::size_t objs = 0;
        std::size_t consecutive = 1;
        std::size_t sizeClass = smallSizeClasses[i];
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
        out = sbrk(slab);
#else
        out = malloc(slab);
#endif // __linux__
        assert(out != nullptr);
        assert(consecutive > 0);
        std::unique_lock writeLockArena(mutArena);
        arenaUsedPages.emplace(getPage(out), i, consecutive, true);
        writeLockArena.unlock();
        std::unique_lock writeLockBin(b.mutBin);
        b.binFreeChunks.emplace(out, objs);
    }
    mellocPrint("arena %zu inited ", this->id);
}