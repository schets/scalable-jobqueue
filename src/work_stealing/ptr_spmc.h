#pragma once

#include <atomic>
#include <memory>

#include "../util/buffer_size.hpp"

//this is to be used in conjunction with a private stack -
//the private stack essentially stores the fork stack
//of each task. It remains unmodifed by all this stealing
//the queue, on the other hand, is where other threads get their pointers
//the producer will occasionally check the size and add, 
//or will be informed if the queue runs out
class _ptr_spmc {
	constexpr static size_t arrsize = 256;
	constexpr static size_t arrmod = arrsize - 1;
	std::unique_ptr<void *[]> ptrs;
	//put ptrs in unique cache line
	buffer_size<64, decltype(ptrs)>::buffer b1;

	//data for stealing crapola
	std::atomic<size_t> head;
	std::atomic<size_t> tail_cache;

	buffer_size<64, std::atomic<size_t>>::buffer b2;

	std::atomic<size_t> tail;
	size_t head_cache;

	size_t reload_tail_cache(size_t ctail);

public:

	//!rapid, but too small space estimate for the producer
	size_t prod_space_est() {
		return head_cache -
			tail.load(std::memory_order_relaxed);
	}

	//slower but more accurate space estimate.
	size_t space_est() {
		return head.load(std::memory_order_relaxed) -
			tail.load(std::memory_order_relaxed);
	}

	bool enqueue(void *ptr);

	bool enqueue_n(void **myptrs, size_t nenqueue);

	//!returns nullptr on failure
	void *deque();



	_ptr_spmc();
	~_ptr_spmc();
};

