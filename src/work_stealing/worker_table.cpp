#include "worker_table.h"

namespace work_stealing {
namespace _private {

constexpr static id_type empty_val = -1;
constexpr static id_type untouched_val = -2;

constexpr static int64_t empty_task = -1;

constexpr static size_t max_tries = 3;


struct worker_table::table_elem {
	std::atomic<int64_t> task_id;

	//normal pointer because I know it's size
	std::atomic<id_type> *workers;

	rw_lock listlock;

	table_elem() :
		task_id(-1),
		workers(nullptr) {}

	void init(id_type which, size_t wksize) {
		listlock.init();
		workers = new std::atomic<id_type>[wksize];
		workers[0].store(which, std::memory_order_relaxed);
		for (size_t i = 1; i < wksize; i++) {
			workers[i].store(untouched_val, std::memory_order_relaxed);
		}
	}
	//must use a manual destructor...
	void destroy() {
		task_id = -1;
		if (workers) {
			delete[] workers;
		}
		listlock.destroy();
	}
};


//murmur3 32 bit finalizer
static uint32_t hash_task(uint32_t inval) {
	inval ^= inval >> 16;
	inval *= 0x85ebca6b;
	inval ^= inval >> 13;
	inval *= 0xc2b2ae35;
	inval ^= inval >> 16;
	return inval;
}

//read up the list of entries, overwrite the first nonpositive entry
static void add_worker_into(std::atomic<id_type> *wks, size_t wksize, id_type worker, rw_lock* lck) {

	//so, this DOES work with other threads
	//trying to get spots.
	//If a thread is looking through the array,
	//then there must be an open spot above it
	//a thread that starts looking afterwards cannot steal
	//all of the spots above the worker - since one is either
	//open below the worker (the new thread could have opened one below),
	//and the new thread won't pass it. Or, one could be opened above,
	//in which case both pass each other as much as they want, 
	//there are enough spots.

	//requires read lock
	//since this CANNOT
	//be made consistent with filtering out values of something being the very last
	lck->acquire_read();
	for (size_t i = 0; i < wksize; i++) {
		auto &curid = wks[i];
		//need a proper reference to do cas with
		auto empty = empty_val;
		auto cval = curid.load(std::memory_order_relaxed);
		if (cval < 0) {
			if (curid.compare_exchange_strong(cval,
											  worker,
											  std::memory_order_relaxed,
											  std::memory_order_relaxed)) {
				break;
			}
		}
	}
	lck->release_read();
}

static void remove_worker(std::atomic<id_type> *wks, size_t wksize, id_type worker, rw_lock *lck) {
	//this doesn't actually require a lock
	//until untouched-val shenanigans start happening
	//since this is simply modifying a single one,
	//which only the current thread will modify
	for (size_t i = 0; i < wksize; i++) {
		auto &curid = wks[i];
		if (curid.load(std::memory_order_relaxed) == worker) {
			//uses release to insure that if this worker takes a
			//different spot, it's visible to other threads
			//that it left a spot open
			if (i == (wksize - 1)) {
				//by definition is the last
				curid.store(untouched_val, std::memory_order_release);
				return;
			}

			//this may/may not be the last
			curid.store(empty_val, std::memory_order_release);

			//now, see what's going on above
			//we want to ensure that the untouched val
			//fills past the top of the array
			//such that the searching threads
			//know when to stop looking
			auto &upperid = wks[i + 1];

			//acquire a read lock to ensure there are no active 'emptiers'
			lck->acquire_read();
			//since there can be no active emptiers at this time
			//this test will accurately show if that next one is the last
			if (upperid.load(std::memory_order_relaxed) != untouched_val) {
				return; //we aren't last, nor is there an empty element above us
			}

			lck->release_read();
			//ok, acquire write lock
			//at the time of the reader lock,
			//the element above us was the last one
			lck->acquire_write();

			//we are now the only thread doing any sort of weird modifications
			//other threads may perform simple removes of themselves,
			//but those won't affect the correctness of this
			//in fact, this will stop once it reads any sort of nonzero value
			//as a result of that, nothing will be taking our empty
			//elements and updating them

			//check to make sure nothing happened before getting the lock

			if ((upperid.load(std::memory_order_relaxed) != untouched_val)
				|| curid.load(std::memory_order_relaxed) != empty_val) {
				//some other threads did work while waiting on the lock
				//either the current index was updated to be something else,
				//or another thread won the lock and made curid untouchable,
				//or the next element up is occupied
				lck->release_write();
				return;
			}



			//relaxed, non cas stores since we have the lock and everything is good
			curid.store(untouched_val, std::memory_order_relaxed);
			if (i == 0) {
				lck->release_write();
				//we are the first, go on our merry way and exit
				//nothing else to store
				return;
			}

			//i must be greater than 0
			//also, curid is the current value of id
			//which is already handled
			do {
				i--;
				auto &someid = wks[i];
				if (someid.load(std::memory_order_relaxed) != empty_val) {
					//we have an occupied point!
					break;
				}
				someid.store(untouched_val, std::memory_order_relaxed);
			} while (i > 0);
			lck->release_write();
			return;
		}
	}
}



worker_table::worker_table() {
}


worker_table::~worker_table() {
}

worker_table::table_elem &worker_table::lookup_existing(task_id task, table_elem *elems, size_t arsize) {

	//this function is only called for already existing
	//and still in-progress tasks
	//therefore,
	//the task that we request must exist in the table
	//and
	//since we must have acquired ownership of such task
	//it can't finish, and therefore be delisted
	//this will never take more times than how many probes
	//can be done before exiting, so not really infinite

	//although for general use, linear probing is bad,
	//it works very well in this case
	//table is sparsely populated, so will have low collisions
	//and linear probing means that when there is one,
	//the data gets loaded into the cache pronto!
	auto index = hash_task(task) % arsize;
	for (;;) {
		auto &cur_struct = elems[index];
		if (cur_struct.task_id.load(std::memory_order_relaxed) == task) {
			return cur_struct;
		}
		index = (index + 1) % arsize;
	}
}

void worker_table::add_worker_to(task_id task, id_type worker) {
	auto ref_tbl = cur_tbl.load(std::memory_order_consume);
	auto as = ref_tbl.size();
	auto& mytbl = lookup_existing(task, ref_tbl.get(), as);
	add_worker_into(mytbl.workers, wksize, worker, &mytbl.listlock);
}

void worker_table::remove_worker_from(task_id task, id_type worker) {
	auto ref_tbl = cur_tbl.load();
	auto as = ref_tbl.size();
	auto& mytbl = lookup_existing(task, ref_tbl.get(), as);
	remove_worker(mytbl.workers, wksize, worker, &mytbl.listlock);
}

//inserts the specified element into the array
//if so, assume the element doesn't exist already
//and nobody is trying to insert it but me
//also, nobody will try and access this element until after
//this exits
void worker_table::start_task(task_id task, id_type worker) {
	lock.acquire_read();
	auto arrsize = cur_tbl.size();
	//acquire table
	auto tbl = cur_tbl.get();
	auto index = hash_task(task) % arrsize;
	for (size_t i = 0; i < max_tries; i++) {
		auto &cur_elem = tbl[i];
		//check if even possible
		auto cur_id = cur_elem.task_id.load(std::memory_order_relaxed);
		if (cur_id == empty_task) {
			//try to cas in my id!
			if (cur_elem.task_id.compare_exchange_strong(cur_id,
														 task,
														 std::memory_order_relaxed,
														 std::memory_order_relaxed)) {


				cur_elem.init(worker, wksize);
				lock.release_read();
				return;
			}
		}
		index++;
		index = index % arrsize;
	}
	//we have failed thus far to insert a value. shit. try resizing
	lock.release_read();

	//try to resize?
	if (write_setup.try_lock()) {
		//we acquired the lock - do some resizing!!
		resize();
		write_setup.unlock();
	}
	else {
		//lock, and then wait on the mutex
		//as a ghetto condition variable
		write_setup.lock();
		write_setup.unlock();
		//and try re-inserting on the new table
		start_task(task, worker);
	}
}

//removes the specified element from the array
//this is super easy, we know that nothing else will be
//looking for this index
//since the task is completed
//nobody will steal for it, lookup it's workers, etc.
void worker_table::end_task(task_id task) {
	auto ref_ptr = cur_tbl.load();
	auto& mytbl = lookup_existing(task, ref_ptr.get(), ref_ptr.size());
	mytbl.task_id.store(empty_task, std::memory_order_relaxed);
	mytbl.destroy();
}

//!when this is called, assume that the element exists
size_t worker_table::find_active_workers(id_type *out, size_t maxn, task_id which) {
	auto ref_hold = cur_tbl;
	auto& mytbl = lookup_existing(which, ref_hold.get(), ref_hold.size());
	size_t cind = 0;
	auto tbl = mytbl.workers;

	//it's ok if this ends up inconsistent with the final list,
	//it's basically just a way of estimating which workers to query
	for (size_t i = 0; i < wksize; i++) {
		auto curid = tbl[i].load(std::memory_order_relaxed);
		if (curid < 0) {
			//some sort of empty value
			if (curid == untouched_val) {
				//we are at the end of the array!
				break;
			}
			//just a blank spot
			continue;
		}
		//we have an actual worker!
		//Write it into the output
		out[cind] = curid;
		cind++;
	}
	return cind;
}


void worker_table::resize() {
	//already have lock on write setup at this point - don't needs locking and all that
	auto new_tbl = cur_tbl.load();
	auto ref = cur_tbl.get();
	auto newsize = cur_tbl.size();
	auto retry = true;
	do {
		newsize *= 2;
		new_tbl = shared_array<table_elem>(newsize);
		lock.acquire_write();
		auto tbl = new_tbl.get();
		for (size_t i = 0; i < wksize; i++) {
			auto& elem = ref[i];
			if (elem.task_id.load(std::memory_order_relaxed) != empty_task) {
				//insert into table
				auto new_ind = hash_task(elem.task_id) % newsize;
				bool inserted = false;
				for (size_t j = 0; j < 3; j++) {
					auto &new_elem = tbl[new_ind];
					if (new_elem.task_id == empty_task) {
						memcpy(&new_elem, &elem, sizeof(new_elem));
						inserted = true;
						break;
					}
				}
				if (!inserted)
					goto retry_resize;
			}
		}
		cur_tbl = new_tbl;
		lock.release_write();
		break;
		//looooooooool fix this i'm too lazy
	retry_resize:
		lock.release_write();
	} while (1);
}

} // namespace _private
} // namespace work_stealing