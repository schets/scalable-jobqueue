#include "ptr_deque.h"

constexpr const static size_t block_size = page_allocator<4096>::block_size;
constexpr const static size_t node_size = block_size - (2 * sizeof(std::atomic<void *>) + sizeof(page_allocator<4096>::page));
constexpr const static size_t pointers = node_size / sizeof(void *);
struct ptr_deque::deque_node {
	void *elements[pointers];
	page_allocator<4096>::page pref;
	std::atomic<deque_node *> next;
	std::atomic<deque_node *> prev;
};

ptr_deque::ptr_deque(page_allocator<4096> &allocer) : alloc(allocer) {

}


ptr_deque::~ptr_deque() {

}

bool ptr_deque::isempty() {
	//these loads may be made inconsistent,
	//but the inconsistencies will cause
	//an incorrect empty result, not corruption
	auto topn = top.load(std::memory_order_relaxed);
	auto topi = top_index.load(std::memory_order_relaxed);

	auto boti = bottom_index.load(std::memory_order_acquire);
	auto botn = bottom.load(std::memory_order_relaxed);

	if ((topn == botn) && ((boti == topi) || (boti == topi + 1)))
		return true;
	if ((topn->next.load(std::memory_order_acquire) == botn)
		&& (boti == 0) && (topi == pointers - 1))
		return true;
	return false;
}

void ptr_deque::push_bottom(void *topush) {
	auto bnode = bottom.load(std::memory_order_relaxed);
	auto cindex = bottom_index.load(std::memory_order_relaxed);
	//business as usual
	if (cindex != -1) {
		bnode->elements[cindex] = topush;
		//might loosen is possible...
		bottom_index.store(cindex - 1, std::memory_order_release);
	}

	//need to allocate a new node
	else {
		auto npage = alloc.get_page();
		deque_node* dptr = (deque_node *)npage.get_ptr();
		dptr->pref = npage;
		dptr->prev.store(nullptr, std::memory_order_relaxed);
		dptr->next.store(bnode, std::memory_order_relaxed);
		bnode->prev.store(dptr, std::memory_order_relaxed);
		bottom.store(dptr, std::memory_order_relaxed);
		bottom_index.store(pointers - 1, std::memory_order_release);
	}
}

bool ptr_deque::pop_top(void *& outptr) {
	if (isempty())
		return false;

	auto curi = top_index.load(std::memory_order_acquire);
	if (curi != -1) {

	}
}