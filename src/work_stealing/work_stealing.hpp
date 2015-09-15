#pragma once
//game plan
/**
each worker task pushes into private deque
this may not happen if there are other inactive workers
then tasks will go directly to them

but, excluding that case,
it's a plain private deque
there will be a spmc queue (bounded)
which some of these tasks will get pushed into
the worker will push tasks in at the beginning,
and otherwise, only do so when tasks are stolen
and the size gets too small

this allows a clean and effecient deque implementation
that avoids any sort of concurrency and will be faster

This is good since most work is not stolen
and exists only in the private deque

When going to steal work, a worker will do 1 of two things - 

1. Try to take a task from the input queue. This will increase throughput,
   at the cost of per-task latency. Then go for most recent task,
   or steal from some random workers

2. Search for the highest priority tasks (oldest task),
   and try to steal work from workers processing that

   This will make each task finish more quickly, but there will be more
   tasks stolen and may have a noticable affect on throughput.
   That will be especially true in benchmarks which flood
   the queue with worthless tasks, since there will be many
   stolen tasks that generate very few children (and incur very little non-steal work)
*/

#include <atomic>
#include <stdint.h>
#include <stddef.h>
#include <unordered_map>
#include <algorithm>
#include <deque>
#include "task.hpp"
#include "types.hpp"

namespace work_stealing {

template<class C>
class work_stealing {
	using namespace _private;

	typedef _private::task<C> task;
	//static helpers

	static bool acquire_local(task &ints);

	//CONSTANTS

	//unassigned task id
	constexpr static id_type noid = -1;

	/**
	 message statuses
	*/

	/**
	task statuses
	*/
	constexpr static flag_t done = 1;
	constexpr static flag_t given_away = 1 << 2;
	constexpr static flag_t in_prog = 1 << 3;
	constexpr static flag_t traverse = 1 << 4;
	constexpr static flag_t local_steal = 1 << 5;
	constexpr static flag_t dealloc = 1 << 6;
	constexpr static flag_t open_msg = 1 << 7;
	constexpr static flag_t distributed = 1 << 8; //not used yet...

	struct message {
		//assume will never be odd aligned
		//so stuff status into the first bit
		std::atomic<task *>& myref;
		message(std::atomic<task *>& r) : myref(r) {
			r.store(nullptr, std::memory_order_relaxed);
		}

		//tries to kill the message,
		//is up to caller to deal with refcounts
		bool kill_message() {
			auto curv = myref.load(std::memory_order_relaxed);
			//someone has 'activated' the message
			if (curv) {
				return;
			}
			//set the pointer to 1 - therefore nothing else will touch it
			//relaxed is valid - there is no synchronization (yet)
			//since only one thread will actually execute this...
			myref.compare_exchange_strong(curv,
										  (task *)1,
										  std::memory_order_relaxed,
										  std::memory_order_relaxed);
		}

		bool set_message(task *tsk) {
			auto curs = myref.load(std::memory_order_relaxed);

			//someone has already set the pointer
			if (curs) {
				return false;
			}

			//again, no ordering needed here - can only succeed by one actor
			return myref.compare_exchange_strong(curs,
												 tsk,
												 std::memory_order_relaxed,
												 std::memory_order_relaxed);
		}

		const task *view_message(std::memory_order ord = std::memory_order_consume) {
			//I think I could get away with relaxed
			//since no ordering/syncronization going on here
			//(only atomicity)
			//but consume definitely works
			auto cmess = myref.load(ord) & (~1);
			if (cmess) {
				return *cmess;
			}
			return nullptr;
		}

		task *get_message(std::memory_order ord = std::memory_order_consume) {
			auto cmess
		}
	};

	class context {
		task_context *ctx;
		uint32_t added_tasks;
		uint32_t task_id;

	public:
		template<class ...Args>
		void fork(Args...&& ctors);
		void sync();
	};

	struct task_context {

		std::deque<task> subtasks;
		size_t tid;
		std::vector<std::atomic<task *>> message_from;

		buffer_size<
			//unbounded_mpsc
			std::deque<task>,
			size_t,
			std::vector<std::atomic<task *>>
		>::buffer b2;

		//task where anyone can put some task, with a possible id request
		std::atomic<task *> global_mess;
		std::atomic<id_type> taskid;
	};

	class iworker {
		task *alloc_task();
		void dealloc_task(task *to_dealloc);

		task *pop_task();

		void execute_task(task *tsk);

		//unbounded_mpsc for recieving messages
		std::unordered_map<size_t, task_context> active_tasks;
		work_stealing *holder;
		task_context& active_task;
	public:

		void add_task(task *tsk);

		//!performs any cleanup that may need cleaning up
		//!reclaiming stolen tasks, deallocating tasks that had other viewers
		//!deallocating memory from unordered freelists?
		void cleanup();

		void run();

		friend class context;
	};


	void request_for_from(id_type from, id_type taskid, std::atomic<task *> &mytask);
	void request_for(id_type taskid, std::atomic<task *> &mytask);
	void request_any(std::atomic<task *> &mytask);

	//!wastes time spinning
	void loop_cycles(size_t ncycles);

public:

	work_stealing() {
	}

	virtual ~work_stealing() {
	}
};

template<class C>
template<class ...Args>
void work_stealing<C>::context::fork(Args...&& ctors) {
	auto tsk = holder->alloc_task();
	new (tsk) task(cur, std::forward<Args>(ctors...));
	added_tasks++;

	//not required just yet - no tree walking
	//!add the task to the list of children
	//tsk->next.store(cur->children, std::memory_order_relaxed);
	//cur->children.store(tsk, std::memory_order_release);

	//add task to local deque
	holder->add_task(tsk);
}

//!This is the sync where no other thread can be crawling the tree
//and for the first implementation, no threads at all will be able to
//crawl the tree. Will only be message based.

//newish game plan
//for now - for non-stealing tree
//store work items in std::deque, mega-optimization not needed yet
//don't use freelist, but instead just normal deque allocator
//and pop all at once - use an iterator to examine tasks in the deque
//for the stealing version, will need allocators
//ownership will be across multiple threads and creator
//will lose references.
//but for now, simple message-based system
template<class C>
void work_stealing<C>::context::sync() {
	task *running = nullptr;
	std::atomic<task *> stolen;

	//get back of iterator
	auto curtsk = ctx->subtasks->end() - added_tasks;
	auto taskend = ctx->subtasks->end();
	std::for_each(curtsk, taskend, [&, =this](task& tsk) {
		//Tasks can be deallocated here
		//since the executed tasks don't have read access to the callable itself
		//As a result, extracted data will need to be held externally anyways

		//!we test for the ability to get a task
		//acquire_local since we are not executing in a stolen context
		if (acquire_local(tsk)) {
			execute_task(tsk);
			holder->dealloc_task(tsk);
		}

		//Check to see if the task is finished - 
		//local steal is only set when a worker 'steals' from itself
		//so if here is reached, then the task must be done. Read can be
		//relaxed since all of same thread.
		//Otherwise, we check to see if another thread has finished with it.
		//This is acquire since all writes to the task must be visible
		//(and finished) when we deallocate the task and inform the worker
		//that the task is finished
		else if (tsk->read_status(local_steal, std::memory_order_relaxed)
				 || tsk->read_status(done, std::memory_order_acquire)) {
			holder->dealloc_task(tsk);
		}

		//At this point, the only valid state for the task to be in
		//is in-progress and stolen by another thread
		else {
			needs_work = true;
			//activate message requesting work
			//put in-progress into list
			auto stealer = tsk->wid.load(std::memory_order_relaxed);
			request_for_from(stealer);
			tsk->extra.store((uintptr_t)running, std::memory_order_relaxed);
			running = tsk;
		}
	});

	if (running) {
		//submit specific request to global queue
		ctx->cleanup();
	}

	running = dump_finished(running);
	task *from_stack;
	while (running && got_from_stack(from_stack)) {
		if (acquire_local(from_stack)) {
			from_stack->set_unsync(local_steal);
			execute_task(from_stack);
		}
		running = dump_finished(running);
	}

	//see if any of the requests were satisfied
	//really don't want to get here - involves
	//some proper memory ordering always
}

//work_stealing static helpers

template<class C>
static bool work_stealing<C>::acquire_local(task &tsk) {
	//Check to see if the status has been made visible to another thread
	//if not, avoid any synchronization codes - very expensive 
	//and for most cases here won't be used commonly
	if (!tsk.read_status(given_away, std::memory_order_relaxed)) {
		//task is not visible to other threads
		//do syncless version
		return !tsk.set_unsync(in_prog, std::memory_order_relaxed);
	}
	else {
		tsk.acquire_status(in_prog, std::memory_order_relaxed);
	}
}

template<class C>
void work_stealing<C>::loop_cycles(size_t ncycles) {
	volatile size_t rand_counter = ncycles;
	volatile size_t loop iter;
	for (loop_iter = 0; loop_iter < ncycles; loop_iter++) {
		//do some work to waste a bit of time
		rand_counter = ((rand_counter + 5) * 737) % 0xcafebabe;
	}
}

} // namespace work_stealing