#pragma once
#include <stdint.h>
#include <memory>
#include <atomic>
#include <mutex>

#include "types.hpp"
#include "../util/rw_lock.h"
#include "../util/buffer_size.hpp"
//This class is used to perform lookups on which workers
//are working on a given task
//lock free hash table, maps a task id to a list
//to an array of workers that work on the task
//uses linear probing b/c the hash table will usually be
//quite empty - ~n_workers-2*n_workers elements will exist
//within the table at any given time
//in this case, there are few collisions,
//and when there are, the next elements
//in the array are loaded into the cache

//wait free on adding workers to existing,
//rw lock on resizing, low contention
//and low load so practically wait free on
//insert new
namespace work_stealing {
namespace _private {

class worker_table {

	struct table_elem {
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
			workers[0] = which;
			for (size_t i = 1; i < wksize; i++) {
				workers[i] = -2;
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

	std::shared_ptr<table_elem> cur_tbl;
	buffer_size<64, decltype(cur_tbl)>::buffer b1;

	size_t arrsize;
	size_t wksize;

	buffer_size<64, size_t, size_t>::buffer b2;
	rw_lock lock;

	buffer_size<64, rw_lock>::buffer b4;
	//mutex so that writer setup
	//doesn't have to block other reads/writes
	//only the actual table move does
	std::mutex write_setup;

	table_elem &lookup_existing(task_id tsk, table_elem *elems);

	void resize();
public:
	void add_worker_to(task_id task, id_type worker);
	void remove_worker_from(task_id task, id_type worker);

	void start_task(task_id task, id_type worker);
	void end_task(task_id task);

	size_t find_active_workers(id_type *out, size_t num, task_id which);
	worker_table();
	~worker_table();
};

} // namespace _private
} // namespace work_stealing