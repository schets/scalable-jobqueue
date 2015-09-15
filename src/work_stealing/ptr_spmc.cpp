#include "ptr_spmc.h"



_ptr_spmc::_ptr_spmc() {

}


_ptr_spmc::~_ptr_spmc() {
}

bool _ptr_spmc::enqueue(void *p) {
	auto ctail = tail.load(std::memory_order_relaxed);
	if ((ctail - head_cache) > (arrsize - 1)) {

		head_cache = head.load(std::memory_order_relaxed);
		if ((ctail - head_cache) > (arrsize - 1)) {
			return nullptr;
		}
	}

	ptrs[(ctail + 1) & arrmod] = p;

	//ordering is stored with release to ensure that the write to
	//ptrs is visible
	tail.store(ctail + 1, std::memory_order_release);
	return true;
}


bool _ptr_spmc::enqueue_n(void **ps, size_t n) {
	auto ctail = tail.load(std::memory_order_relaxed);
	if ((ctail - head_cache) > (arrsize - n)) {
		head_cache = head.load(std::memory_order_acquire);
		if ((ctail - head_cache) > (arrsize - n)) {
			return nullptr;
		}
	}
	//could write a faster copy
	//one that found the two (or one)
	//regions of the array being written to and 
	//used memcpy, or something that could be vectorized
	//but for sizes that are 10-20 pointers,
	//I doubt that's gonna have a big effect,
	//will wait on profiling for this one
	for (size_t i = 0; i < n; i++) {
		ptrs[(i + ctail) & arrmod] = ps[i];
	}
	tail.store(ctail + n, std::memory_order_release);
	return true;
}

size_t _ptr_spmc::reload_tail_cache(size_t ctail) {
	size_t newtail;
	//loads the current tail and stores it into the cache
	//fine if some inconsistencies here
	//similar to reloading the head cache, all wrong values will simply
	//underrepresent the number of elements in the queue, and may randomly fail
	//this will be quite rare though!
	newtail = tail.load(std::memory_order_acquire);
	tail_cache.store(newtail, std::memory_order_relaxed);
	return newtail;
}

void *_ptr_spmc::deque() {

	//chead and ctail both reloaded in cas loop
	auto chead = head.load(std::memory_order_relaxed);
	auto ctail = tail_cache.load(std::memory_order_relaxed);
	void *rptr;
	do {
		//test against cache, make sure to reload
		if (chead >= (ctail - 1)) {
			//this can effectively be treated as an acquire load on tail
			ctail = reload_tail_cache(ctail);
			if (chead >= (ctail - 1)) {
				return false;
			}
		}
		rptr = ptrs[chead & arrmod];

		//the cas needs to write with release
		//since when tail loads head into head_cache,
		//the read from ptrs cannot be reordered
	} while (head.compare_exchange_weak(chead,
										chead + 1,
										std::memory_order_release,
										std::memory_order_relaxed));
	return rptr;
}