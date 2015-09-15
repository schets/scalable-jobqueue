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

	buffer_size<64, decltype(tail)>::buffer b2;

	//rarely used, except when tail is made to be nillers
	decltype(head) link;

public:
	void push(task<C> *topush);
	void push_many(task<C> *head, task<C>* tail);

	task<C> *pop();

	template<class Lambda>
	void apply_to_all(const Lambda& fnc);
};


template<class C>
void ptr_mpsc<C>::push(task<C> *topush) {
	push_many(topush, topush);
}

template<class C>
void ptr_mpsc<C>::push_many(task<C> *nhead, task<C> *ntail) {
	nhead->next.store(nullptr, std::memory_order_relaxed);

	auto lnk = link.load(std::memory_order_relaxed);
	auto oldhead = head.exchange(nhead, std::memory_order_acq_rel);
	//can store relaxed, won't be reordered with regards to
	//the exchange (or a later exchange), and it's ok if it gets reordered with
	//a later store to nullptr. The pushing thread shall never
	//access nhead, ntail, and between anyways
	oldhead->next.store(ntail, std::memory_order_relaxed);

}

template<class C>
task<C> *ptr_mpsc<C>::pop() {
	auto ctail = tail.load(std::memory_order_consume);
	auto next = ctail->next.load(std::memory_order_relaxed);

	if (next == nullptr) {
		return nullptr;
	}

	tail.store(next, std::memory_order_release);
	return next;
}

template<class C>
template<class Lambda>
void ptr_mpsc<C>::apply_to_all(const Lambda &fnc) {
	auto ctail = tail.load(std::memory_order_consume);
	auto next = ctail->next.load(std::memory_order_consume);
	while (next) {
		fnc(ctail);
		ctail = next;
		next = ctail->next.load(std::memory_order_consume);
	}
}

} // namespace _private
} // namespace work_stealing
#endif
