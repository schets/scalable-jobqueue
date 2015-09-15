#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include "types.hpp"

namespace work_stealing {

namespace _private {
template<class C>
struct task {
	C fnc;
	std::atomic<task<C> *> next; //tasks have many ll uses
	task_id task_id;
	std::atomic<flag_t> flags;
	id_type wid;

	template<class ...Args>
	task(task *p, id_type wid, Args...&& ctors) : fnc(std::forward<Args>(ctors...)), parent(p) {
		next.store(nullptr, std::memory_order_relaxed);
		flags.store(0, std::memory_order_relaxed);
	}

	//!Sets the status bits, and returns true if at least one flag wasn't set
	bool acquire_status(flag_t flag,
						std::memory_order order);

	//!Reads the status flag with the specified ordering, returns if at least one is true
	bool read_status(flag_t flag,
					 std::memory_order order);

	//!Reads all of the status flags, returns true if all true
	bool read_all(flag_t flags,
				  std::memory_order order);

	//!Sets the specified status flag and returns the previous status
	bool set_status(flag_t flag,
					std::memory_order order);

	//!Sets the status w/o atomics
	bool set_unsync(flag_t flag);
};

template<class C>
bool task::acquire_status(flag_t status, std::memory_order ord) {

	//test status in a relaxed manner to avoid atomics
	if (read_all(status, std::memory_order_relaxed)) {
		//already has status, don't waste time on atomics
		return false;
	}
	return !set_status(status, ord);
}

template<class C>
bool task::read_status(flag_t status, std::memory_order ord) {
	return status & flags.load(ord);
}

template<class C>
bool task::read_all(flag_t status, std::memory_order ord) {
	return (flags.load(ord) & status) == status;
}

template<class C>
flag_t task::set_status(flag_t status, std::memory_order ord) {
	return status & flags.fetch_or(status, ord);
}

template<class C>
flag_t task::set_unsync(flag_t status) {
	auto cstat = flags.load(std::memory_order_relaxed);
	flags.store(cstat | status, std::memory_order_relaxed);
	return status & cstat;
}

} // namespace _private
} // namespace work_stealing