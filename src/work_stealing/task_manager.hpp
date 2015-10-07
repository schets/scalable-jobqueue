#pragma once
#include "../util/buffer_size.hpp"
#include "ptr_spmc.h"
#include "task.hpp"

#include <algorithm>
#include <utility>
#include <atomic>

namespace work_stealing {
namespace _private {

//This class manages the deque of stacks,
//the stack of allocated pointers, and the 
//task allocator itself

/***********
Task manager for large tasks
This uses a linked list deque
and doesn't actually reallocate
and manager a separate pointer stack
In addition, the tasks allocated for 
placement into the steal queue
are the same as each linked list element,
to avoid managing other queues. This works
since each task is already allocated independently
***********/

template<class C>
class task_manager {

	//constants
	//holding too many is very bad
	//if too many are on the stack,
	//that memory may be essentially dead,
	//only to get revived later while it's
	//not in the cache at all
	//otoh, having few is still fine - 
	//most of the allocations are in the bottom of the tree,
	//where these tasks will be recycled over and over again
	//it won't waste memory, and allocations are more
	//likely to be in the cache
	constexpr size_t s_ratio = 4096 / sizeof(C);
	constexpr size_t max_hold = s_ratio < 8 ? 8 : s_ratio;
	struct task_blob {
		task<C> task;
		union {
			//Holds next/prev points for 
			//tasks residing in the deque
			struct {
				struct task_blob *next, *prev;
			} in_deque;

			//holds next pointer for
			//a task residing in the allocator
			struct {
				struct task_blob *next, *prev;
			} in_alloc;

			//holds next pointer for
			//a task residing in the stealable section
			struct {
				task_blob *next_steal; //next one for stolen tasks
				task_blob *next_stack; //next one in private stack
			} stealable;

		} state_data;
	};

	_ptr_spmc steal_queue;
	buffer_size<64, _ptr_spmc>::buffer b1;

	task_blob *steal_stack;

	size_t nheld;
	task_blob *de_head, *de_tail;
	task_blob *held_head, *held_tail;


	buffer_size<64, size_t, void *, void *, void *, void *>::buffer b1;

	void put_on_steal_queue(task_blob *blb, size_t n);
	void put_on_steal_stack(task_blob *blb);
	task_blob *get_a_task();
public:

	template<class ...Args>
	void add_task(Args...&& ar);

	bool get_private(task<C> *&tsk);
	void return_private(task<C> *tsk);

	task_deque() {}

	~task_deque() {}
};

template<class C>
void task_manager<C>::put_on_steal_stack(task_blob *b) {
	b->state_data.stealable.next_stack = steal_stack;
	steal_stack = b;
}

template<class C>
task_manager<C>::task_blob *task_manager<C>::get_from_steal_stack() {
	if (steal_stack) {
		auto retb = steal_stack;
		steal_stack = steal_stack->state_data.stealable.next_stack;
		return retb;
	}
	return nullptr;
}

//pushes from the blob onto the pointer
template<class C>
void task_mananger<C>::put_on_steal_queue(task_blob *b, size_t n) {
	task_blob *blb_arr[16];
	auto cur_q = steal_queue.prod_space_est();
	if (n == 0) {
		return;
	}
	if (cur_q > n) {
		cur_q = steal_queue.space_est<true>();
	}
	
	//could optimize away some checks here
	//but will see if it's an actual bottleneck
	while ((n > 0) && b) {
		size_t cind = 0;
		max_push = n > block_size ? block_size : n;
		while (b && (cind < block_size)) {
			blb_arr[cind++] = b;
			push_on_steal_stack(b);
			b = b->state_data.in_deque.prev;
		}
		n -= cind;
		//dump data into the queue
		//and into the local pointer queue
		steal_queue.enqueue_n(blb_arr, cind);
	}

	//all_done!
}

template<class C>
task_manager<C>::task_blob *task_manager<C>::get_a_task() {
	if (held_head) {
		nheld--;
		auto rval = held;
		held_head = held_head->state_data.in_alloc.next;
		if (held_head) {
			held_head->state_data.in_alloc.prev = nullptr;
		}
		return rval;
	}
	return al_alloc(sizeof(task_blob), 64);
}

template<class C>
void task_manager<C>::return_a_task(task_blob *p) {
	nheld++;
	auto holdp = p;
	p->state_data.in_alloc.next = held_head;
	p->state_data.in_alloc.prev = nullptr;
	//must we set the tail as well?

	held_head = p;
	if (nheld > max_hold) {
		//dump the tail, the oldest
		//we do this instead of dumping the new one
		//since the new one is probably fresh from the cache
		auto delval = held_tail;
		held_tail = held_tail->state_data.in_alloc.prev;
		al_free(delval);
	}
	//we must check the tail for setting as well
	else if (!held_tail) {
		held_tail = holdp;
		p->state
	}
}

template<class C>
template<class ...Args>
void task_manager<C>::add_task(Args...&& ar) {
	task_blob *mytask = get_a_task();
	mytask->state_data.in_deque.prev = de_head;
	de_head->state_data.in_deque.next = mytask;
	mytask->state_data.in_deque.next = nullptr;
	de_head = mytask;
	new (&mytask->task) task<C>(std::forward<Args>(ar)...);
}

template<class C>
bool task_manager<C>::get_private(task<C> *&tsk) {
	if (!de_head) {
		return false;
	}
	tsk = &(de_head->task);

	auto rptr = de_head;
	rptr->state_data.in_deque.next = 0;
	rptr->state_data.in_deque.prev = 0;
	de_head = de_head->prev;
	return true;
}

template<class C>
size_t task_manager<C>::steal_n(task<T> *holder, size_t n=1) {
	return steal_queue.deque_n(holder, n);
}

template<class C>
void task_manager<C>::return_private(task<C> *tsk) {
	tsk->task.~task();
	return_a_task((task_blob *)tsk);
}

/**********

Implementation for small objects

**********/



} // namespace _private
} // namespace work_stealing