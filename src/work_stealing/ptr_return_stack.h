#ifndef PTR_RETURN_STACK_H
#define PTR_RETURN_STACK_H

#include <atomic>
#include <utility>

#include "task.hpp"

namespace work_stealing {
namespace _private {

//supports bulk removal of list,
//also bulk inserts given head and tail.
//don't know if will use
template<class C>
class ptr_return_stack {

	std::atomic<task<C> *> head;

	using std::pair<std::atomic<task<C> *>,
					std::atomic<task<C> *>> = ptr_pair;
public:

	//pretty standard push onto a linked stack...
	//also! no aba head - aba comes about
	//as a problem associated with multiple producers,
	//and cas loops on the pop. Being a push,
	//this isn't affected by aba:
	//a transition from (B <- A) to (B <- C <- A)
	//is fine for the push, it doesn't actually modify
	//the contents of A
	void push(task<C> *topush) {
		push({topush, topush});
	}

	//fairly standard stack push
	void push(const ptr_pair &head_tail) {
		auto nhead = head_tail.first;
		auto ntail = head_tail.second;
		auto chead = head.load(std::memory_order_consume);
		do {
			ntail.next.store(chead, std::memory_order_relaxed);
		} while (!head.compare_exchange_weak(chead,
											 nhead,
											 std::memory_order_release,
											 std::memory_order_relaxed));
	}

	//This pop doesn't suffer from the aba problem
	//since it doesn't have a cas loop - 
	//it acquires the whole stack in a single instruction

	//The aba problems occurs with a cas loop in the following case
	//stack is (B <- A). Thread one loads A, then loads A->next (B).
	//Thread two then pops A, pushes C, then pushes A again.
	//Thread one successfully performs a cas and replaces head with B
	//since it believes A unchanged. C is now lost.

	//It's fairly easy to see how there is no ABA problem in this case -
	//XCHG will always succeed - if it does so before the push cas,
	//the push fails and starts from nullptr. otherwise, 
	task<C> *pop() {
		auto chead = head.load(std::memory_order_relaxed);

		if (chead == nullptr) {
			return;
		}

		//as explained earlier,
		//this atomically grabs a snapshot of the stack
		//and allows pushers to start anew as well
		return head.exchange(nullptr, std::memory_order_acq_rel);
	}
};



} // namespace work_stealing
} // namespace _private
#endif