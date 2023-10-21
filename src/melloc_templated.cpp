/**
 * @file melloc.cpp
 * 
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Custom memory allocator
 * @version 1.0
 * @date 2023-09-05
 * 
 * 
 * melloc.cpp contains descriptor for melloc allocator class. Meets std::allocator
 * requirements.
 * 
 * Uses arena allocator with distinct size classes. Each thread is assigned an
 * arena in round-robin fashion from first allocation and owns its own thread
 * cache, which stores recently freed chunks.
 * 
 * Thread cache decays exponentially based on wall timer tick. Inserting or 
 * removing from thread cache resets the decay amount to the lowest. Allocating 
 * from the thread cache is lock-free.
 * 
 * Objects are rounded up to nearest size class and allocated from a slab, which
 * keeps them contiguous. Only the start of a slab (which can span multiple pages)
 * and the number of consecutive pages involved is stored in red-black tree, rather
 * than each page, to minimize metadata. Similarly, objects are not directly 
 * stored in red-black tree, which sacrifices some part of valid delete checking
 * to achieve less metadata.
 * 
 * 
 */



//#include <bits/stdc++.h>
//#include <unistd.h>

// todo tunable parameters for how many objs per mmap? (rn its 32)

#include <utility>
#include <memory>
#include <iostream>
#include <mutex>
#include <set>
#include <map>
#include <unordered_map>
#include <array>
#include <thread>

#include "melloc.h"


#define PAGE_SIZE           (4096U) // todo: can make system call to grab this
#define PAGE_MASK           (0xFFFFF000)

/*  Larger thread cache will have less peak locking overhead, but more peak
    metadata used  */
#define THREAD_CACHE_SIZE   (128)



using namespace std;



struct Melloc {
    using value_type        = T;
    using pointer           = value_type*;
    using const_pointer     = value_type const*;
    using void_pointer      = void*;
    using size_type         = size_t; // optional: todo
    using difference_type   = size_t; // optional: todo
    using Page              = uint64_t;

    // todo: stubbed until use actual syscalls
    static pointer sbrk(size_t bytes) {
        cout << "====== SBRK CALLED FOR " << bytes << " bytes ==============" << endl;
        return static_cast<pointer>(malloc(bytes));
    }

    static constexpr std::array<size_t, 28> smallSizeClasses {
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

    friend struct Arena;
    struct Arena {
        struct ArenaPageDescriptor {
            /*  Keeps track of pages as either slabs, or parts of "Large"
                size class objects. */

            ArenaPageDescriptor() = delete; 
            
            ArenaPageDescriptor(Page page) : page(page) {}

            ArenaPageDescriptor(Page page, size_t binIdx, size_t consecutive, bool isSlab)
                : page(page)
                , binIdx(binIdx)
                , consecutive(consecutive)
                , isSlab(isSlab) {}
            
            inline bool operator <(const ArenaPageDescriptor& other) const noexcept {
                return page < other.page;
            }

            Page    page;
            size_t  binIdx;
            size_t  consecutive;    /* including itself */
            bool    isSlab;
        };

        friend struct Bin;
        struct Bin {
            Bin() {}

            pointer allocate() {
                std::scoped_lock lock(mutBin);
                size_t sizeClass = smallSizeClasses[binIdx];
                pointer out = nullptr;

                // first look through free list
                if (!binFreeChunks.empty()) {
                    auto chunkIterator = binFreeChunks.begin();
                    out = chunkIterator-> /* ptr */ first;
                    if (--chunkIterator-> /* consecutive */ second > 0) {
                        /* Change key of binFreeChunks without realloc using node handle*/
                        cout << " decremented chunk " << sizeClass << " iterator, now count is " << chunkIterator->second << endl;
                        auto nh = binFreeChunks.extract(chunkIterator);
                        nh.key() = increment(out, sizeClass);
                        binFreeChunks.insert(std::move(nh));
                    }
                    else {
                        cout << " removed chunk " << sizeClass << " iterator " << endl; 
                        binFreeChunks.erase(chunkIterator);
                    }
                }
                // otherwise, ask OS for slab (some contiguous pages)
                else {
                    size_t objs = 1;
                    size_t consecutive = 0;
                    if (sizeClass < THREAD_CACHE_SIZE) {
                        // Get one page
                        consecutive = 1;
                        out = sbrk(PAGE_SIZE);
                        objs = PAGE_SIZE / sizeClass;
                        cout << "Bin sz " << sizeClass << " asked for one pages " << endl;
                    }
                    else {
                        size_t slab = (32*sizeClass) & PAGE_MASK;
                        consecutive = slab / PAGE_SIZE;
                        out = sbrk(slab);
                        objs = slab / sizeClass;
                        cout << "Bin sz " << sizeClass << " asked for " << slab << " bytes " << endl;
                    }
                    myArena->arenaUsedPages.emplace(getPage(out), binIdx, consecutive, true);
                    binFreeChunks.emplace(increment(out, sizeClass), objs - 1);
                }
                //cout << " bin::allocate() is returning " << out << endl; // VS long lines
                return out;
            }
            
            void giveBack(pointer ptr) {
                size_t sizeClass = smallSizeClasses[binIdx];

                /* check if we can merge existing free entries */
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

            // Bin members
            size_t                      binIdx;
            std::mutex                  mutBin;
            /*  binFreeChunks stores pointers to available chunks, along
                with how many consecutive free pages are after it. We preferentially
                allocate from the lowest address of binFreeChunks */
            std::map<pointer, size_t>   binFreeChunks;
            Arena*                      myArena;
        }; // struct Bin

        /*  A ThreadCache stores the recently freed chunks for each thread, which
        we scan prior to allocating through the Arena, to reduce peak lock
        contention */
        friend struct ThreadCache;
        struct ThreadCache {
            using BinCache = std::array<pointer, THREAD_CACHE_SIZE>;
            
            void push(pointer ptr, size_t sizeClassIdx) noexcept {
                decayRate[sizeClassIdx] = 1;
                size_t topIdx = topIdxs[sizeClassIdx];
                if (topIdx < THREAD_CACHE_SIZE) {
                    ++topIdxs[sizeClassIdx];
                    cache[sizeClassIdx][topIdx] = ptr;
                }
                else {
                    /* if no space, then return to bin immediately */
                    myArena->bins[sizeClassIdx].giveBack(ptr);
                }
            }

            /*  Exponentially decays existing buffer, slowly at first */
            inline void decay(size_t sizeClassIdx) noexcept {
                topIdxs[sizeClassIdx] -= std::min(topIdxs[sizeClassIdx], decayRate[sizeClassIdx]);
                decayRate[sizeClassIdx] <<= 1;
            }

            pointer pop(size_t sizeClassIdx) noexcept {
                decayRate[sizeClassIdx] = 1;
                size_t topIdx = topIdxs[sizeClassIdx];
                if (topIdx > 0) {
                    --topIdxs[sizeClassIdx];
                    return cache[sizeClassIdx][topIdx];
                }
                return nullptr;
            }

            std::array<BinCache, sizeof(smallSizeClasses)>      cache;
            std::array<size_t, sizeof(smallSizeClasses)>        topIdxs {0};
            std::array<size_t, sizeof(smallSizeClasses)>        decayRate {0};
            Arena*                                              myArena;
        }; // struct ThreadCache
    

        Arena() {
            init();
        }

        [[nodiscard]]
        pointer allocate(size_t binIdx) {
            return bins[binIdx].allocate();
        }

        void deallocate(pointer ptr) noexcept {
            auto pageIt = arenaUsedPages.lower_bound(getPage(ptr));
            if (pageIt == arenaUsedPages.end() ||
                pageIt->page + pageIt->consecutive*PAGE_SIZE <= getPage(ptr)) {
                /*  todo: freeing from a thread other than the original assignee is 
                    stubbed for now. Shouldn't be doing this anyways */
                return;
            }

            /*  Large chunks are not thread cacheable */ 
            if (!pageIt->isSlab) {
                // todo: free large obj using munmap and madvise
                return;
            }
            
            /*  Small or medium chunks are thread cacheable */
            size_t binIdx = pageIt->binIdx;
            std::thread::id tid = std::this_thread::get_id();
            ThreadCache threadCache = threadCaches[tid];
            threadCache.push(ptr, binIdx);
        }

        void init() noexcept {
            /*  Populate all bins */
            for (int i = 0; i < bins.size(); ++i) {
                Bin& b = bins[i];
                b.myArena = this;
                b.binIdx = i;
                //cout << " starting bin construction " << b.sizeClass << endl;
                pointer out = nullptr;
                size_t sizeClass = smallSizeClasses[i];
                size_t objs = 1;
                size_t consecutive = 0;
                if (sizeClass < 128) {
                    // Get one page
                    consecutive = 1;
                    out = sbrk(PAGE_SIZE);
                    //cout << " out of sbrk1 " << sizeClass << endl;
                    objs = PAGE_SIZE / sizeClass;
                    //cout << "Bin sz " << sizeClass << " asked for one pages " << endl;
                }
                else {
                    size_t slab = (32*sizeClass) & PAGE_MASK;
                    consecutive = slab / PAGE_SIZE;
                    objs = slab / sizeClass;
                    out = sbrk(slab);
                    //cout << " out of sbrk2 " << sizeClass <<  endl;
                    //cout << "Bin sz " << sizeClass << " asked for " << slab << " bytes " << endl;
                }
                arenaUsedPages.emplace(getPage(out), i, consecutive, true);
                b.binFreeChunks.emplace(out, objs);
                //cout << " bin constructed " << sizeClass << endl;
            }
        }

        // Arena members
        size_t                              id          {0};
        std::mutex                          mutArena;     // for large allocs
        std::array<Bin, sizeof(smallSizeClasses)/sizeof(size_t)>
                                            bins;
        /* arenaPages tracks the size class associated with a page */
        std::set<ArenaPageDescriptor>       arenaUsedPages;

        /* threadIdCache tracks the thread cache associated with a thread ID */
        std::unordered_map<std::thread::id, ThreadCache>
                                            threadCaches;
    };




    /*  ThreadCachePurger runs on a timer tick? and moves items from a ThreadCache
        back into its Arena 
    struct ThreadCachePurger {

    };
    */

public:
    /* Constructor */
    Melloc() {};

    /* Copy constructor */
    template <class U>
    Melloc(const Melloc<U>& other) noexcept;
    
    constexpr bool operator ==(const Melloc& other) {
        /* Each instance of a memory allocator ties into a */
        return false;
    }

    constexpr bool operator !=(const Melloc& other) {
        return !(operator==(other));
    }


    /* Allocate memory */
    [[nodiscard]]
    pointer allocate(size_type n) {
        size_t sz = roundup(n * sizeof(value_type)); // must be done within melloc to check if large size
        std::thread::id tid = std::this_thread::get_id();

        /* Large objects handled by kernel */
        if (isLargeSize(sz)) {
            return sbrk(sz);
        }
        
        /* Small and medium objects handled by arena */
        auto arenaIt = mapArenas.find(tid);
        if (arenaIt == mapArenas.end()) {
            /*  First allocation for this thread, assign via round-robin.
                Initialize thread cache as well */
            arenaIt = mapArenas.emplace(tid, getArena()).first;
            arenas[arenaIt->second].threadCaches[tid].myArena = &(arenas[arenaIt->second]);
        }
        Arena& arena = arenas[arenaIt->second];
        size_t binIdx = getBinIdx(sz);

        /* First check the thread cache */
        pointer out = arena.threadCaches[tid].pop(binIdx);
        if (out) {
            return out;
        }

        /* If thread cache has nothing, ask arena and do bin locking */
        return arena.allocate(binIdx);
    }

    /*  Free memory. Caller is responsible for ensuring the address is valid (ie.
        has previously been returned by allocate()), else undefined behavior, 
        like in malloc() interface standard. */
    void deallocate(pointer ptr, size_t) noexcept {
        auto arenaIt = mapArenas.find(std::this_thread::get_id());
        if (arenaIt == mapArenas.end()) return;
        Arena& arena = arenas[arenaIt->second];
        arena.deallocate(ptr);
    }

    /*  include Melloc.construct() for compliance with C++17 or earlier */
    template<class... Args>
    static constexpr void construct(Melloc& a, T* p, Args&&... args) {
        new((void*)p) T(std::forward<Args>(args)...);
    }

    /*  include Melloc.destroy() for compliance with C++17 or earlier */
    void destroy(pointer ptr) {
        ptr->~T();
        // todo madvise unload page
    }

    [[nodiscard]]
    size_t getArena() const noexcept {
        return 0; // todo: stub until multithread.
    }

    inline static Page getPage(pointer addr) noexcept {
        return (reinterpret_cast<Page>(addr) & PAGE_MASK);
    }

    /*  return the bin index corresponding to a particular size. */
    static size_t getBinIdx(size_t sz) noexcept {
        size_t bin = 0;
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
    static size_t roundup(size_t sz) noexcept {
        if (isLargeSize(sz)) {
            cout << "   " << sz << " rounds up to " << ((sz + PAGE_SIZE) & PAGE_MASK) << endl;
            bool isOffPage = !(sz == (sz & PAGE_MASK));
            return (sz + PAGE_SIZE*isOffPage) & PAGE_MASK;
        }
        cout << "   " << sz << " rounds up to bin " << getBinIdx(sz) << " with value " << smallSizeClasses[getBinIdx(sz)] << endl;
        return smallSizeClasses[getBinIdx(sz)];
    }

    inline static bool isLargeSize(size_t sz) noexcept {
        return sz >= PAGE_SIZE;
    }

    /*  Pointer address arithmetic */
    inline static pointer increment(pointer ptr, size_t sz) noexcept {
        //cout << "  incrementing " << ptr << " by " << sz << " gives " << static_cast<char*>(ptr) - sz << endl;
        return static_cast<char*>(ptr) + sz;
    }

    /*  Pointer address arithmetic */
    inline static pointer decrement(pointer ptr, size_t sz) noexcept {
        //cout << "  decrementing " << ptr << " by " << sz << " gives " << static_cast<char*>(ptr) - sz << endl;
        return static_cast<char*>(ptr) - sz;
    }

    // Melloc members
    std::unordered_map<std::thread::id, size_t>         mapArenas;
    std::array<Arena, 1>                                arenas;
};

int main() {
    Melloc<char> alloc;
    auto p = std::allocator_traits<Melloc<char>>::allocate(alloc, 4);
    std::allocator_traits<Melloc<char>>::construct(alloc, p, 189);
    std::allocator_traits<Melloc<char>>::destroy(alloc, p);

    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 7);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 8);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 9);

    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 15);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 16);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 17);

    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 255);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 256);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 257);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 555);

    
    for (int i = 0; i < 40; ++i) {
        p = std::allocator_traits<Melloc<char>>::allocate(alloc, 555);
    }

    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 4096);
    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 25750);
    std::allocator_traits<Melloc<char>>::deallocate(alloc, p, 0);

    p = std::allocator_traits<Melloc<char>>::allocate(alloc, 25750);




    cout << " OK " << endl;
    return 0;
}