/**
 * @file atomic_guard.h
 *
 * @author Gavin Dan (xfdan10@gmail.com)
 * @brief RAII wrapper for atomic flag
 * @version 1.0
 * @date 2023-09-25
 *
 * 
 * RAII wrapper for atomic flag
 *
 */

#ifndef UTIL_MELLOC_ATOMIC_GUARD_H
#define UTIL_MELLOC_ATOMIC_GUARD_H

#include <atomic>
#include <array>

#include "arena.h"
#include "melloc_defs.h"
#include "melloc_utils.h"


class AtomicFlagGuard {
public:
	AtomicFlagGuard() = delete;

	inline explicit AtomicFlagGuard(std::atomic_flag& f) noexcept : flg(f) {
		while (flg.test_and_set()) {
			/*	std::atomic::wait() implemented on Linux using futex
				Probably faster than cond var using mutex:
				https://www.modernescpp.com/index.php/performancecomparison-of-condition-variables-and-atomics-in-c-20/ */
			
			flg.wait(true);	/* block until we can set flag false->true */
		}
	}

	inline ~AtomicFlagGuard() noexcept {
		flg.clear();
		flg.notify_one();
	}

private:
	std::atomic_flag& flg;
};



#endif // UTIL_MELLOC_ATOMIC_GUARD_H