#ifndef MPSC_HPP_
#define MPSC_HPP_

#include "../util/buffer_size.hpp"

#include <atomic>

#include "task.hpp"

namespace work_stealing {
namespace _private {

//!mpsc queue used for transfering pointers back to original destination
template<class C>
class ptr_mpsc {

	std::atomic<task<C> *> tail;

	buffer_size<64, decltype(tail)>::buffer b1;

	decltype(tail) head;

public:
	void push(task<C> *topush);
	task<C> *pop();
};


template<class C>
void ptr_mpsc<C>::push(task<C> *topush) {
	topush->next.store(nullptr, std::memory_order_relaxed);
	auto prev = head.exchange(topush, std::memory_order_acq_rel);
	prev->next.store(topush, std::memory_order_release);
}

template<class C>
task<C> *ptr_mpsc<C>::pop() {
	auto ctail = tail.load(std::memory_order_relaxed);
	auto next = ctail->next.load(std::memory_order_acquire);

	if (next == nullptr) {
		return nullptr;
	}

	tail.store(next, std::memory_order_release);
	return next;
}

} // namespace _private
} // namespace work_stealing
#endif
