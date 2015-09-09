#include "ptr_mpsc.h"
#include "../allocator/block_allocator.hpp"

constexpr static size_t ave_mem = page_allocator<4096>::block_size - (sizeof(void *) + sizeof(page_allocator<4096>::page));
constexpr static size_t ptr_p_block = ave_mem / 8;

struct ptr_mpsc::block {
	void * ptrs[ptr_p_block];
	page_allocator<4096>::page pref;
	std::atomic<block *> next;
};

void ptr_mpsc::push(void *topush) {

}

void *ptr_mpsc::pop() {
	if (tail == ptr_p_block - 1) {
		if (!advance_tail()) {
			return nullptr;
		}
	}
	if (tail_block != head_block.load(std::memory_order_relaxed)) {
		return pop_nocont();
	}
	return pop_cont();
}

void *ptr_mpsc::pop_nocont() {
	//!known to be non-empty, on different block than head
	auto retv = tail_block->ptrs[tail++];

	//can free tail?
	if (tail == ptr_p_block - 1) {
		advance_tail();
	}
	return retv;
}

bool ptr_mpsc::advance_tail() {
	auto tb = tail_block;
	auto tnext = tail_block->next.load(std::memory_order_acquire);
	if (!tnext)
		return false;
	auto temp = tail_block;
	tail_block = tnext;
	alloc.release_page(tail_block->pref);
	return true;
}

ptr_mpsc::block *ptr_mpsc::get_page() {
	auto npage = alloc.get_page();
	block *blk = (block *)npage.get_ptr();
	blk->pref = npage;
	return blk;
}