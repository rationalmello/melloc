/**
 * @file melloc.h
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Melloc header
 * @version 1.0
 * @date 2023-09-05
 *
 *
 */

#ifndef UTIL_MELLOC_H
#define UTIL_MELLOC_H

#include <atomic>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
#ifdef __linux__
#include <sys/mman.h>
#include <signal.h>
#endif // __linux__

#include "arena.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


#ifdef __linux__
extern void threadDescriptorSignalHandler(int sig, siginfo_t* si, void* uc);
#endif // __linux__


class Melloc {
    using pointer       = void*;
    using Page          = uint64_t;

    struct ThreadDescriptorWrapper;
    struct ThreadDescriptor;

    friend struct Arena;
    struct Arena {
        struct PageDescriptor {
            PageDescriptor() = delete;

            PageDescriptor(Page page) : page(page) {}

            /*  Construct for large object */
            PageDescriptor(Page page, std::size_t len, bool isSlab)
                : page(page)
                , isSlab(isSlab)
            {
                assert(!isSlab);
                sizeInfo.len = len;
            }

            /*  Construct for slab */
            PageDescriptor(Page page, std::size_t binIdx, std::size_t consecutive, bool isSlab)
                : page(page)
                , isSlab(isSlab)
            {
                assert(isSlab);
                sizeInfo.slab.binIdx = binIdx;
                sizeInfo.slab.consecutive = consecutive;
            }

            inline bool operator <(const PageDescriptor& other) const noexcept {
                /*  Backwards comparator for use with std::lower_bound().
                    Use case: we want the nearest page in arenaUsedPages that's 
                    smaller or equal to our page, rather than greater or equal */
                return page > other.page;
            }

            struct Slab {   
                size_t  binIdx;
                size_t  consecutive; /* including itself */
            };

            union SizeInfo {
                size_t  len;    /* Only used if isSlab is not set */
                Slab    slab;   /* Only used if isSlab is set */
            };

            // PageDescriptor members
            SizeInfo    sizeInfo    {0};
            Page        page        {0};
            bool        isSlab      {false};
        }; // struct PageDescriptor

        /*  A bin owns a slab and tracks free chunks for every small size class */
        friend struct Bin;
        struct Bin {
            Bin() {}

            void* allocate();

            void giveBack(void* ptr);

            // Bin members
            std::size_t                     myArena;
            std::size_t                     binIdx;
            std::mutex                      mutBin;
            /*  binFreeChunks stores pointers to available chunks, along
                with how many consecutive free pages are after it. We preferentially
                allocate from the lowest address of binFreeChunks */
            std::map<void*, std::size_t>  binFreeChunks;
        }; // struct Bin

        Arena();

        [[nodiscard]]
        void* allocate(std::size_t sz, ThreadDescriptorWrapper& tdw);

        void deallocate(void* ptr) noexcept;

        void init();

        // Arena members
        std::size_t                                 id {0};
        std::array<Bin, smallSizeClasses.size()>    bins;
        std::set<PageDescriptor>                    arenaUsedPages;
        std::shared_mutex                           mutArena;
    }; // struct Arena

    /*  Wrapper class for ThreadDescriptor */
    friend struct ThreadDescriptorWrapper;
#ifdef __linux
    friend void threadDescriptorSignalHandler(int sig, siginfo_t* si, void* uc);
#endif
    struct ThreadDescriptorWrapper {
        ThreadDescriptorWrapper() = delete;

        inline explicit ThreadDescriptorWrapper(std::thread::id tid) {
            td = std::make_unique<Melloc::ThreadDescriptor>(tid);
        }
        
        /*  Uses unique_ptr, so copy constructor not allowed */
        ThreadDescriptorWrapper(const ThreadDescriptorWrapper& other) = delete;

        /*  Move constructor called during std::map::rehash() */
        inline ThreadDescriptorWrapper(ThreadDescriptorWrapper&& other) noexcept {
            std::swap(td, other.td);
        }

        ~ThreadDescriptorWrapper() = default;

        inline ThreadDescriptor* operator->() {
            return &(*td);
        }

        inline bool operator ==(const ThreadDescriptorWrapper& other) const noexcept {
            assert(td && other.td);
            return (td->tid == other.td->tid);
        }

        struct hash {
            inline size_t operator() (std::thread::id tid) const noexcept {
                return std::hash<std::thread::id>{} (tid);
            }
        };

    private:
        std::unique_ptr<ThreadDescriptor> td {nullptr};
    }; // struct ThreadDescriptorWrapper

    /*  A ThreadDescriptor most importantly stores the recently freed chunks per
        thread, which we scan prior to allocating through the Arena, to reduce peak 
        lock contention */
    friend struct ThreadDescriptor;
    struct ThreadDescriptor {
        using BinCache = std::array<void*, THREAD_CACHE_SIZE>;

        ThreadDescriptor() = delete;

        ThreadDescriptor(const ThreadDescriptor& other) = delete;

        explicit ThreadDescriptor(std::thread::id tid);

        ~ThreadDescriptor() {};

        void pushCache(void* ptr, std::size_t sizeClassIdx) noexcept;

        void* popCache(std::size_t sizeClassIdx) noexcept;

        void purge();

        void decay(std::size_t sizeClassIdx) noexcept;

        // ThreadDescriptor members
        std::size_t                                             myArena;
        std::thread::id                                         tid;
        std::array<BinCache, smallSizeClasses.size()>           cache       {0};
        std::array<std::size_t, smallSizeClasses.size()>        topIdxs     {0};
        std::array<std::size_t, smallSizeClasses.size()>        decayRate   {0};
        std::array<std::atomic_flag, smallSizeClasses.size()>   usedFlags;
#ifdef __linux__
        struct sigaction                                        sa;
        struct sigevent                                         sev;
        struct itimerspec                                       its;
        timer_t                                                 timerObj;
#endif
    }; // struct ThreadDescriptor

public:
    /* Constructor */
    Melloc();

    /* Copy constructor */
    Melloc(const Melloc& other) = delete;

    /* For compliance with std::allocator, can remove */
    inline constexpr bool operator ==(const Melloc& other) {
        return false;
    }

    inline constexpr bool operator !=(const Melloc& other) {
        return !(operator==(other));
    }

    /* Allocate memory */
    [[nodiscard]]
    static void* allocate(std::size_t n);

    /* Free memory */
    static void deallocate(void* ptr) noexcept;

private:
    /* Assign arena */
    [[nodiscard]]
    static std::size_t getArena() noexcept;

    /*  return the bin index corresponding to a particular size. */
    static std::size_t getBinIdx(std::size_t sz) noexcept;

    /*  round up to nearest small or large size class. */
    static std::size_t roundup(std::size_t sz) noexcept;

    void decay() noexcept;

    void init() noexcept;

    // Melloc members
#ifndef NDEBUG
public:
    static std::mutex                                           mutPrint;
private:
#endif // NDEBUG
    static std::shared_mutex                                    mutMelloc;
    static std::array<Arena, NUM_ARENAS>                        arenas;
    static std::unordered_map<std::thread::id,
                              ThreadDescriptorWrapper,
                              ThreadDescriptorWrapper::hash>    threadDescriptors;
    bool                                                        globalInit {false};
};


#endif // UTIL_MELLOC_H