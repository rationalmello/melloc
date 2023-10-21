/**
 * @file demo.cpp
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief Demo for memory alloc internal workings
 * @version 1.0
 * @date 2023-09-10
 *
 *
 * Be sure to build in Debug mode (or else NDEBUG flag may be auto-defined
 * by build system and the internal prints won't work)
 *
 *
 */


#include <utility>
#include <memory>
#include <iostream>
#include <mutex>
#include <set>
#include <map>
#include <unordered_map>
#include <array>
#include <thread>
#include <iostream>

#include "melloc.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


using namespace std; // just for demo to make life easier


int main() {
    mellocPrint("================= starting main =================");
    Melloc alloc;

    void* p = nullptr;
    for (int i = 0; i < 40; ++i) {
        p = Melloc::allocate(3000);
        Melloc::deallocate(p);
    }

    *(static_cast<long long*>(p)) = 1LL;
    mellocPrint(" p is %i", *(static_cast<long long*>(p)));
    Melloc::deallocate(p);

    mellocPrint("allocating and freeing large object");
    p = Melloc::allocate(30000);
    Melloc::deallocate(p);

#ifdef __linux__
    for (;;) {
        pause(); // wait for timer tick to happen
    }
#endif // __linux__

    return 0;
}