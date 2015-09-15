#pragma once

#include <atomic>

#include "types.hpp"
#include "shared_array.hpp"

namespace work_stealing {
namespace _private {
//similar to worker table
//uses ref counted array
//for wait-free reads, single writer
//means reads can work along with writes and resizes
//also simply membership checks, no dual inserting
//but, readers look for elements which may not exist

//work on later - has some fairly specific functionality
class single_writer_set {

	struct table_elem {
		std::atomic<int64_t> task_id;
		std::shared_ptr<void> mydata;
	};

	shared_array<table_elem> data;
	void resize();
public:

	single_writer_set();
	~single_writer_set();

	std::shared_ptr<void> get_task_queue(task_id tsk);

	void add_task(task_id tsk, std::shared_ptr<void> ptr_to);
	void remove_task(task_id tsk);

};

} // namespace _private
} // namespace work_stealing
