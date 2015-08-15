#ifndef WORK_QUEUE_HPP_
#define WORK_QUEUE_HPP_

#include "spmc.hpp"
#include <thread>
#include <vector>

//Uses a single queue per thread
//but upon failure to get work
//a thread goes and looks for other threads
//while possible to have a single wait free queue
//will pay the costs of contention. Or at least we will see...
//now
//in the real world
//that's a-ok since work done by threads is going to be longer than
//the cost of a memory read. But it's more interesting to make these
//things highly optimized

//avoiding virtual calls for now, is it worth it?
//would make simpler, indirect call prediction would help
//will benchmark this version first, and unless this is < 15-20
//ns/push-pop, will try virtualizing it

template<class Callable>
class sp_work_queue {

	struct work_queue{
        spmc_queue<Callable> queue;
        alignas(64) uint64_t id;
        uint64_t _counter; //used to make work stealing more 'random'
		std::atomic<bool> alignas(64) needs_wakeup;
		void needs_notify();
		void un_notify();
    };

	//this could point contention at a single source...
	//use as a last resort
    std::atomic<uint64_t> recent_work_stole;

	std::atomic<uint64_t> current_tasks;
    
    size_t current_queue;
    std::vector<work_queue> queues;

	void run_worker(work_queue& worker);
	bool steal_some_work(work_queue& worker, Callable& cl);
	bool steal_from_last_stolen(work_queue& worker, Callable& cl);
	bool steal_from_all(work_queue& worker, Callable& cl)

	//!Returns true if the queue is not accepting any more jobs
	bool is_dead();
public:
    bool add_job(const Callable& jb);
};

template<class Callable>
void sp_work_queue<Callable>::work_queue::needs_notify() {
	needs_wakeup.store(true, std::memory_order_release);
}

template<class Callable>
void sp_work_queue<Callable>::work_queue::un_notify() {
	needs_wakeup.store(false, std::memory_order_release);
}

template<class Callable>
bool sp_work_queue<Callable>::add_job(const Callable& jb) {
    for (size_t tries = 0; tries < n_queues * 2; tries++) {
        auto& curq = queues[current_queue];
        current_queue = (current_queue + 1) % n_queues;
        if (curq.try_push(jb))
            return true;
    }
    return false;
}

template<class Callable>
bool sp_work_queue<Callable>::run_worker(work_queue& worker) {
	while !(is_dead()) {
		Callable mycl;
		
		//I use continue here to avoid deeply nested ifs
		//it is purely a stylistic choic.
		
		//Work was available - perform it and go onto the next iteration
		if (worker.queue.try_pop(mycl)) {
			mycl();
			continue;
		}

		//No work in the queue - check out some other queues
		//using 'advanced' scheduling

		if (steal_some_work(worker, cl)) {
			mycl();
			continue;
		}

		//Take a look at a queue which recently had work stolen

		if (steal_from_last_stolen(worker, cl)) {
			mycl();
			continue;
		}

		//claim notification comes before final check of queue
		//to avoid race condition where
		//worker needs notification
		//but producer has pushed, and found a false check on notification
		worker.needs_notify();

		if (worker.queue.try_pop(mycl)) {
			worker.un_notify();
			mycl();
			continue;
		}

		//later
		//wait_on(worker)
	}
}

//just get some implementations out there...
//!This can be much smarter
template<class Callable>
bool sp_work_queue<Callable>::steal_some_work(worker& w, Callable& cl) {
	auto id = w.id + w.counter++;
	auto qs = queues.size();
	for (size_t i = id; i < qs; i++) {
		if (queues[i % qs].try_pop(cl))
			return true;
	}
	return false;
}

template<class Callable>
bool sp_work_queue<Callable>::steal_from_last_stolen(worker& w, Callable& cl) {
	auto recst = recent_work_stole.load(std::memory_order_acquire);
	auto& rec = queues[recst];
	return rec.try_pop(cl);
}
#endif
