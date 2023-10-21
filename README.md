# melloc++

melloc++ is a memory allocator written in C++ for Linux with multithread support.
It currently supports allocate and deallocate (like malloc and free). Currently in early stages. The allocator seems to be working from my own informal tests,
which are not extensive, particularly surrounding multithreading. More expansive 
tests/demo to follow.

melloc++ was inspired by Facebook's jemalloc memory management model: https://engineering.fb.com/2011/01/03/core-infra/scalable-memory-allocation-using-jemalloc/


My allocator uses several arenas assigned in round-robin fashion to threads upon
first allocation, which lowers lock contention. It also uses multiple size class bins
within each arena, and asks the kernel for a contiguous slab of pages to carve up into
objects of that size class bin. The metadata is tracked in a std::set using the default
std::allocator (I could implement my own metadata packing method like jemalloc does, but
that's a later thing). When a thread frees an allocation, it first goes to the thread's 
unique thread cache, which is garbage collected in exponentially increasing pieces
when no recent activity has occured for that thread. The thread cache allows
a complete bypass of locking when requesting an object of the same size class
that was recently freed from the same thread. It also uses a wall-clock timer for the
garbage collector passes, because it's inconsistent to make it event-based: the next
event may never come. These ideas were shamelessly stolen from jemalloc because I thought 
they were really good ideas.

Synchronization is achieved using some shared_locks for single-writer, multiple-reader
scenarios (any modification of the metadata, in either global melloc++ or arena or
bin level, requires the writer lock of the appropriate scope). The exceptions are thread
caches, which uses an atomic_flag and the accompanying wait/notify feature (courtesy
of C++20) because I wanted to try it out. In Linux, this is apparently a wrapper around futex.


Future improvements are:

 - More comprehensive tests
 - Changing timer handling for a ton of threads (can have thread-groups, similar to how bins are size-classes, so we don't have a ton of timers)
 - Using a different std::allocator for metadata. Right now I'm using default,
    but I think all modern mallocs (ptmalloc2, dlmalloc, etc.) use some form of
    densely-packed slabs, so metadata stuff (eg. set nodes of fixed size) should
    take advantage of this already on most compilers...


## Demo

The code comes with a simple demo that shows some allocations, deallocations,
and console prints that give an idea of the inner workings if you wish to read
through them. On Linux only, it demonstrates a pass of the garbage collector
running on a wall timer, and how it returns memory to melloc.

### Getting started

To run the demo, you will need to build using the CMake. Navigate to the root directory
and run:

```
$ cmake -E make_directory "build"
$ cmake -E chdir "build" cmake -DCMAKE_BUILD_TYPE=Debug ../
$ cmake --build "build" --config Debug
```

Note that the Debug configuration is necessary for the demo prints to work,
since they are conditionally compiled if the NDEBUG flag is not missing.



