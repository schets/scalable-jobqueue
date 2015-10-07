#include "mutex.h"

namespace work_stealing {

bool mutex::try_lock() {
	//a check to avoid an unneeded lock instr
	//true means someone has it
	if (is_held.load(std::memory_order_relaxed)) {
		return false;
	}

	return is_held.exchange(1, std::memory_order_acquire);
}

void mutex::unlock() {
	is_held.store(0, std::memory_order_release);
}

bool mutex::locked() {
	return is_held.load(std::memory_order_relaxed);
}

//shared mutex operations
//writer_mask, coincidentally,
//happens to be our cas target as well
static constexpr uint64_t writer_mask = (uint64_t)1 << 63;
static constexpr uint64_t waiting_mask = (uint64_t)1 << 62;
static constexpr uint64_t w_w_m = writer_mask | waiting_mask;
static constexpr uint64_t reader_mask = ~writer_mask;

template shared_mutex<writer_priority::favors_readers>;
template shared_mutex<writer_priority::favors_writers>;

static constexpr auto fw = writer_priority::favors_writers;
static constexpr auto fr = writer_priority::favors_readers;

/***

Shared instantations

*/



template<writer_priority>
void shared_mutex::shared_unlock() {
	lock_data.fetch_sub(1, std::memory_order_release);
}

/***

Instantiations for favors readers

*/

template<>
bool shared_mutex<fr>::try_exclusive_lock () {
		//basic reader-writer sanity check
		//if the blob is nonzero, then something else
		//is active
	if (lock_data.load(std::memory_order_relaxed) != 0) {
		return false;
	}

	//cas with 0 to writer_mask
	//to ensure that nothing else
	//gets the writer - for it's
	//only safe to acquire a writer
	//when nothing else exists, which means 0
	//memory_order_acquire is fine here
	//since it's essentially a normal mutex acquire
	uint64_t needed = 0;
	return lock_data.compare_exchange_strong(needed,
										     writer_mask,
										     std::memory_order_acquire,
										     std::memory_order_relaxed);
}



template<>
bool shared_mutex<fr>::try_shared_lock() {

	if (lock_data.load(std::memory_order_relaxed) & writer_mask) {
		//a writer writer is active, jump ship;
		return false;
	}

	//try incrementing...
	//this is done instead of cas because using this method,
	//multiple readers won't block each other
	auto prev_val = lock_data.fetch_add(1, std::memory_order_acquire);

	//return if the writer mask was set once we got the mutex
	//this is safe up to 2^63 readers all conflicting with
	//a writer acquiring the mutex. so should be safe...
	//and the writer will simply reset to zero once all is
	//said and done
	return !(prev_val & writer_mask);
}

template<>
void shared_mutex<fr>::exclusive_unlock() {
	lock_data.store(0, std::memory_order_release);
}

/***

Instatiations for writer-favoring

*/

template<>
bool shared_mutex<fw>::try_exclusive_lock() {
	//basic reader-writer sanity check
	//if the blob is nonzero, then something else
	//is active
	auto needed = lock_data.load(std::memory_order_relaxed);

	//needed == 0 or needed == waiting_mask
	if ((needed & waiting_mask) == needed) {
		if ((needed & waiting_mask) == 0) {
			//no waiters yet - set it up
			lock_data.fetch_or(waiting_mask, std::memory_order_relaxed);
		}
		//already a waiter - abandon ship
		return false;
	}

	//note that this also unsets the writer mask, so that
	//another writer can wait on this one
	bool rval = lock_data.compare_exchange_strong(needed,
				       							  writer_mask,
											      std::memory_order_acquire,
											      std::memory_order_relaxed);
	//success was had, return home
	if (rval) {
		return true;
	}

	if (needed & waiting_mask) {
		//waiting mask is set and cas failed
		//probably another reader going at it
		//won't spinlock here, is responsibility of owner
		return false;
	}
	//set the waiting bit and return false
	lock_data.fetch_or(waiting_mask, std::memory_order_relaxed);
	return false;
}

template<>
bool shared_mutex<fw>::try_shared_lock() {

	if (lock_data.load(std::memory_order_relaxed) & w_w_m) {
		//a writer or waiter is active, jump ship;
		return false;
	}

	//try incrementing...
	//this is done instead of cas because using this method,
	//multiple readers won't block each other
	auto prev_val = lock_data.fetch_add(1, std::memory_order_acquire);

	//return if the writer or waiter mask
	//is set after the operation

	return !(prev_val & w_w_m);
}

template<>
void shared_mutex<fw>::exclusive_unlock() {
	//this or doesn't have to be atomic
	//any readers that act at this state are bogus anyways
	//and if thw waiting bit is set, only this thread will un-set it
	auto cstate = lock_data.load(std::memory_order_relaxed);
	if (cstate & waiting_mask) {
		lock_data.store(waiting_mask, std::memory_order_release);
	}

	//ok, the waiting bit isn't set,
	//so we have to atomically set everything else to zero
	//how? atomic and the waiting mask - if someone had set the waiting mask
	//just prior, would have no effect. Else, if mask was zero, would leave zero
	//all other values erased
	else {
		lock_data.fetch_and(waiting_mask, std::memory_order_release);
	}
}

}