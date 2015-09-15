#pragma once
#include <atomic>
#include "../util/buffer_size.hpp"
#include "../allocator/block_allocator.hpp"

//x86/sparc only
//http://www.cs.technion.ac.il/~mad/publications/asplos2014-ffwsq.pdf
class ptr_deque {
	//similar to std::deque but smaller,
	//only does what I need and has certain retrictions
	struct segmented_array {

	};
	struct deque_node;

	std::atomic<deque_node *> bottom;
	std::atomic<intptr_t> bottom_index;
	page_allocator<4096> &alloc;

	buffer_size<64, std::atomic<deque_node *>, std::atomic<intptr_t>, page_allocator<4096>&>::buffer b1;

	std::atomic<deque_node *> top;
	std::atomic<size_t> top_index;
	std::atomic<size_t> aba;

	bool isempty();

public:
	ptr_deque(page_allocator<4096>& allocer);
	~ptr_deque();
	void push_bottom(void *topush);
	bool pop_bottom(void *& out);
	bool pop_top(void *&out);
};

