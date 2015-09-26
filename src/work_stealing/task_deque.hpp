#pragma once
#include <type_traits>
#include "task.hpp"
namespace work_stealing {
namespace _private {

constexpr static size_t slimit = 256;
template<class C, bool tobig = (sizeof(task<C>) > 256)>
class task_deque;


template<class C, true>
 {
	struct task_blob {
		task<C> task;
		task_blob *next, *prev;
	};

	task_blob *held;
	task_blob *released;
public:

	task_deque() {
	}

	~task_deque() {
	}
};

} // namespace _private
} // namespace work_stealing