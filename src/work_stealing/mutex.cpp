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
	return is_held.store(std::memory_order_relaxed);
}

//shared mutex operations
//writer_mask, coincidentally,
//happens to be our cas target as well
static constexpr uint64_t writer_mask = (uint64_t)1 << 63;
static constexpr uint64_t reader_mask = ~writer_mask;


shared_mutex::try_exclusive_lock () {
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

void shared_mutex::exclusive_unlock() {
	lock_data.store(0, std::memory_order_release);
}

bool shared_mutex:: (a64 &rw) {
		//there is an active writer!
	if (gw(rw).load(std::memory_order_relaxed) != 0) {
		return false;
	}
		//safe below 2^63 active readers -
		//none of those adds can affect a currently active
		//writing bit - that's a sane requirement I think
		//will cause race condition otherwise
		//also, these spurious adds are safe
		//since no reading is done (due to check for writer)
		//and the stuff all works out!
	uint64_t prev_res = rw.fetch_add(1, std::memory_order_acquire);

		//now ensure that the last result DIDN't have a writer yet,
		//since a writer cas may have completed between the check and the add
	return !(prev_res & writer_mask);
}

static void release_reader(a64 &rw) {
		//aaaand subtract out that reader!
		//release to ensure that accesses
		//all finish before the sub, so that way
		//a new access by a writer can't 
		//pollute any of them
	rw.fetch_sub(1, std::memory_order_release);
}

bool shared_mutex::try_exclusive_lock() {

	if (lock_fncs::get_blob(lock_data) != 0) {
		//either a reader or a writer is active, jump ship;
		return false;
	}


}

void shared_mutex::exclusive_unlock() {
	lock_data.store(0, std::memory_order_release);
}

}