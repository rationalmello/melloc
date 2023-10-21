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

#include <melloc.h>
#include <melloc_defs.h>

#ifdef never

/* Perform garbage collection on a timer */
void Melloc::decay() noexcept {
	/* todo Lock the entire map first i guess... ugh. 
		We can get around by decaying each THREAD on a timer?? 
		once we have a handle to the threadDescriptor, we can skip the
		vector and map lookups and jsut call decay from inside there...
	*/
	for (auto [tid, threadDescriptorIdx] : mapThreadDescriptorIdxs) {
		/* also some lock for the vector? */
		ThreadDescriptor& desc = threadDescriptors[threadDescriptorIdx];
		for (int i = 0; i < smallSizeClasses.size(); ++i) {

		}
	}
}

/* the wrapper was going to be used until I found out that unordered map uses
    a literal linked list, with nodes, so it doesn't become invalidated
    when I insert new stuff or when rehashing lollll
*/
friend struct ThreadDescriptorWrapper;
struct ThreadDescriptorWrapper {
    ThreadDescriptorWrapper() = delete;

    explicit ThreadDescriptorWrapper(ThreadDescriptor* td) : td(td) {}

    inline ThreadDescriptorWrapper(const ThreadDescriptorWrapper& other) noexcept {
        td = other.td;
    }
    inline ThreadDescriptorWrapper(ThreadDescriptorWrapper&& other) noexcept {
        td = other.td;
    }

    struct hash {
        size_t operator() (const ThreadDescriptorWrapper& tdw) const noexcept {
            assert(tdw.td);
            return std::hash<std::thread::id>{} (tdw.td->tid);
        }
    };

    inline bool operator ==(const ThreadDescriptorWrapper& other) const noexcept {
        assert(td && other.td);
        return (td->tid == other.td->tid);
    }
private:
    ThreadDescriptor* td{ nullptr };
}; // struct ThreadDescriptorWrapper



#endif