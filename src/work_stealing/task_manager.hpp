#pragma once
#include "../util/buffer_size.hpp"
#include "ptr_spmc.h"
#include "task.hpp"

#include <utility>
#include <atomic>

namespace work_stealing {
namespace _private {

//This class manages the deque of stacks,
//the stack of allocated pointers, and the 
//task allocator itself
constexpr static size_t slimit = 256;
template<class C, bool tobig = (sizeof(task<C>) > 256)>
class task_manager;

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

template<class C, true>
task_manager {

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
	constexpr size_t max_hold = 16;

	struct task_blob {
		task<C> task;
		union {
			//Holds next/prev points for 
			//tasks residing in the deque
			struct {
				task_blob *next, *prev;
			} in_deque;

			//holds next pointer for
			//a task residing in the allocator
			struct {
				task_blob *next;
			} in_alloc;

			//holds next pointer for
			//a task residing in the stealable section
			struct {
				task_blob *next;
			} stealable;

			//holds pointer data for when the task is being returned
			//by another
			struct {
				std::atomic<task_blob *> next;
			} returning;
		} state_data;
	};

	_ptr_spmc steal_queue;
	buffer_size<_ptr_spmc>::buffer b1;

	size_t nheld;
	task_blob *de_head, *de_tail;
	task_blob *held;
	task_blob *released;

	buffer_size<size_t, void *, void *, void *, void *>::buffer b1;

	std::atomic<task_blob *> queue_tail;

	buffer_size<task_blob *>::buffer b2;

	std::atomic<task_blob *> queue_head;


	void put_on_steal_queue(task_blob *blb);
	task_blob *get_a_task();
public:

	template<class ...Args>
	void add_task(Args...&& ar);

	bool get_private(task<C> *&tsk);
	void return_private(task<C> *tsk);

	void return_stolen(task<C> *stolen);

	task_deque() {}

	~task_deque() {}
};

template<class C>
task_manager<C, true>::task_blob *task_manager<C>::get_a_task() {
	if (held) {
		nheld--;
		auto rval = held;
		held = held->in_alloc.next;
		return rval;
	}
	return al_alloc(sizeof(task_blob), 64);
}

template<class C>
void task_manager<C, true>::return_a_task(task_blob *p) {
	if (nheld > max_hold) {
		al_free(p);
	}
	else {
		nheld++;
		p->next = held;
		held = p;
	}
}

template<class C>
template<class ...Args>
void task_manager<C, true>::add_task(Args...&& ar) {
	task_blob *mytask = get_a_task();
	mytask->state_data.in_deque.prev = de_head;
	de_head->state_data.in_deque.next = mytask;
	mytask->state_data.in_deque.next = nullptr;
	de_head = mytask;
	new (&mytask->task) task<C>(std::forward<Args>(ar)...);
}

template<class C>
bool task_manager<C, true>::get_private(task<C> *&tsk) {
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

template<class T, true>
size_t task_manager<C>::steal_n(task<T> *holder, size_t n = 1) {
	return steal_queue.deque_n(holder, n);
}


template<class C>
void task_manager<C, true>::return_private(task<C> *tsk) {
	tsk->task.~task();
	return_a_task((task_blob *)tsk);
}

template<class C>
void task_manager<C, true>::return_stolen(task<C> *tsk) {
	return_private(tsk);
}

/**********

Implementation for small objects

**********/



} // namespace _private
} // namespace work_stealing