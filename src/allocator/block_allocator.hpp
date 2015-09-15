#pragma once
#include <memory>
#include <atomic>
#include <stddef.h>

#include "aligned_alloc.h"
//the allocator deals with blocks of a single size,
//hence the static requirement - keeps it safer
//All blocks are page aligned, the smallest block size is a page
//The actual block size will be enough to be an integer number of pages,
//and hold block metadata

//soon to be globally managing blocks,
//to avoid the global allocator and share pages between
//data

//uses simple cas implementation, since this will have fairly low contention
template<size_t page_size = 4096>
class page_allocator {

public:
	class page {
		uintptr_t block;
	public:

		void *get_ptr() {
			return reinterpret_cast<void *>(block & ~(page_size - 1));
		}
	};

private:

	static_assert(((page_size != 0) && !(page_size & (page_size - 1))),
				  "page_size must be a power of two");
	//stores both the pointer to next, and aba count on the page
	struct page_meta {
		std::atomic<uintptr_t> next;
	};

	//!top of stack
	std::atomic<uintptr_t> first;

	//!number of held blocks
	std::atomic<size_t> num_pages;

	//!maximum number of held pages
	size_t max_pages;

	uintptr_t get_aba(uintptr_t value);
	void incr_aba(uintptr_t &value);

	page alloc_page();
	void free_page(page& pg);
	uintptr_t get_next(uintptr_t current);
	void set_next(uintptr_t& value, uintptr_t next);
public:



	//!size of usable memory region
	static constexpr size_t block_size = page_size - sizeof(page_meta);

	page get_page();
	void release_page(page &inpage);
};


template<size_t page_size>
uintptr_t page_allocator<page_size>::get_aba(uintptr_t value) {
	return value & (page_size - 1);
}

template<size_t page_size>
void page_allocator<page_size>::incr_aba(uintptr_t& value) {
	value = ((get_aba(value) + 1) & (page_size - 1)) | (value & ~(page_size - 1))
}

template<size_t page_size>
uintptr_t page_allocator<page_size>::get_next(uintptr_t value) {
	char *curblock = (char *)get_pointer(value);
	page_meta *meta = curblock + block_size;
	return page_meta->next.load(std::memory_order_relaxed);
}

template<size_t page_size>
void page_allocator<page_size>::set_next(uintptr_t& value, uintptr_t next) {
	if (!value.block) {
		value = next;
		set_next(value, 0);
	}
	else {
		char *curblock = (char *)get_pointer(value);
		page_meta *meta = curblock + block_size;
		page_meta->next.store(next, std::memory_order_relaxed);
	}
}

template<size_t page_size>
typename page_allocator<page_size>::page page_allocator<page_size>::get_page() {
	uintptr_t curhead = first.load(std::memory_order_acquire);
	uintptr_t nexthead;
	do {
		if (!curhead) {
			return alloc_page();
		}
		nexthead = get_next(curhead);
	} while (first.compare_exchange_weak(&curhead,
										 nexthead,
										 std::memory_order_acq_rel,
										 std::memory_order_acquire));
	num_pages.fetch_sub(1, std::memory_order_acq_rel);
	return curhead;
}

template<size_t page_size>
void page_allocator<page_size>::release_page(page &value) {
	if (!value.block)
		return;
	uintptr_t curhead = first.load(std::memory_order_acquire);
	if (num_pages.load(std::memory_order_acquire) >= max_pages) {
		free_page(value);
	}
	do {
		set_next(value, curhead);
	} while (first.compare_and_swap(&curhead,
									value,
									std::memory_order_acq_rel,
									std::memory_order_acquire));
	num_pages.fetch_add(1, std::memory_order_acq_rel);
}

template<size_t page_size>
typename page_allocator<page_size>::page page_allocator<page_size>::alloc_page() {
	uintptr_t mem = reinterpret_cast<uintptr_t>(al_alloc(page_size, page_size));
	page rval;
	rval.block = mem;
	return rval;
}


template<size_t page_size>
void page_allocator<page_size>::free_page(page& pg) {
	al_free(pg.get_pointer());
}