#include "single_writer_set.h"

namespace work_stealing {
namespace _private {

constexpr static size_t max_tries = 3;
constexpr static int64_t empty = -1;

static uint32_t hash_task(uint32_t inval) {
	inval ^= inval >> 16;
	inval *= 0x85ebca6b;
	inval ^= inval >> 13;
	inval *= 0xc2b2ae35;
	inval ^= inval >> 16;
	return inval;
}

std::shared_ptr<void> single_writer_set::get_task_queue(task_id id) {
	auto cur_tbl = data.load();
	auto size = cur_tbl.size();
	auto tbl = cur_tbl.get();
	auto index = hash_task(id) % size;
	for (size_t i = 0; i < 3; i++) {
		auto &cur_elem = tbl[index];
		auto cid = cur_elem.task_id.load(std::memory_order_consume);
		if (cid == id) {
			//still may be false, and element may be waiting for deletion
			//will need to include checks as such
			return std::atomic_load_explicit(&cur_elem.mydata,
											 std::memory_order_relaxed);
		}
		index = (index + 1) % size;
	}
	return nullptr;
}

void single_writer_set::add_task(task_id id, std::shared_ptr<void> toadd) {
	auto size = data.size();
	auto tbl = data.get();
	auto index = hash_task(id) % size;
	for (size_t i = 0; i < 3; i++) {
		auto &cur_elem = tbl[index];
		if (cur_elem.task_id.load(std::memory_order_relaxed) == empty) {
			cur_elem.mydata = toadd;
			cur_elem.task_id.store(id, std::memory_order_release);
		}
	}
	resize();
	add_task(id, toadd);
}

void single_writer_set::remove_task(task_id id) {
	auto tbl = data.get();
	auto size = data.size();
	auto index = hash_task(id) % size;
	for (size_t i = 0; i < 3; i++) {
		auto &cur_elem = tbl[index];
		if (cur_elem.task_id.load(std::memory_order_relaxed) == id) {
			cur_elem.mydata = nullptr;
			//no real ordering going on here anyways - 
			//a lookup can still read a nullptr
			//with any sort of ordering
			//so might as well not release it
			cur_elem.task_id.store(id, std::memory_order_relaxed);
		}
	}
}


void single_writer_set::resize() {
	auto olds = data.size();
	auto news = olds;
	auto oldtbl = data.get();
	//this is a pretty tame use of goto
	//that is similar to a recursive call
try_resize:
	{
		news *= 2;
		shared_array<table_elem> new_data(news);
		auto newtbl = new_data.get();
		for (size_t i = 0; i < olds; i++) {
			auto &cur_elem = oldtbl[i];
			uint32_t ctask = cur_elem.task_id.load(std::memory_order_relaxed);
			if (ctask != empty) {
				auto nind = hash_task(cur_elem.task_id) % news;
				//try to insert the task directly
				size_t i = 0;
				for (; i < 3; i++) {
					auto &new_elem = newtbl[nind];
					auto ntid = new_elem.task_id.load(std::memory_order_relaxed);
					if (ntid != empty) {
						new_elem.task_id.store(ctask, std::memory_order_relaxed);
						new_elem.mydata = cur_elem.mydata;
					}
					nind = (nind + 1) % news;
				}
				//if failed to insert, get even bigger
				if (i >= 3) {
					goto try_resize;
				}
			}
		}
		data.store(new_data, std::memory_order_release);
	}
}

}
}