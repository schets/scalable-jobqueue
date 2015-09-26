#pragma once
#include <vector>

#include "types.hpp"

namespace work_stealing {
namespace _private {

//temporary struct for now
struct prog_task {
	task_id task;

};
//implementing a queue
//for better remove option for non-min values
//are more cache efficient algorithms
//but are more complicated, will just go with
//binomial heap for now
class task_heap {
	std::vector<prog_task> elements;
	//uses a linear search to find the element
	void remove_lin(task_id torm);

	void remove_search(task_id torm);

	size_t get_children_of(size_t tostart);

	void rm_start_from(size_t start_ind);
public:
	task_heap();
	~task_heap();

	const prog_task& at_top();
	prog_task pop();
	void insert(const prog_task& toins);

	void remove(task_id torm);
};

//converts from 0-based array elements
//to 1-based heap, and gets children
template<class T>
size_t priority_heap::get_children_of(size_t of) {
	return 2 * of + 1;
	//return (of + 1) * 2 - 1;
}

template<class T>
T& priority_heap::at_top() {
	return elements[0];
}

template<class T>
T priority_heap::rm_start_from(size_t start) {
	T rval = std::move(elements[start]);
	elements[start] = std::move(elements.back());
	elements.pop_back();
	size_t cind = start;
	size_t child = get_children_of(start);
	while ((child + 1) < end) {
		//do the easy loop while
		//we know that both elements are in the queue
		//swap with first child
		if (elements[child] < elements[cind]) {
			//go down that tree
			move_from = child;
		}
		else if (elements[child + 1] < elements[cind]) {
			move_from = child + 1;
		}
		//is not less than either, stop moving down and return
		else {
			return rval;
		}
		std::swap(elements[cind], elements[move_from]);
		cind = move_from;
		child = get_children_of(cind);
	}
	//at here, we are done or have one last index to check
	if (child < end) {
		if (elements[child] < elements[cind]) {
			std::swap(elements[cind], elements[child]);
		}
	}
	return rval;
}

} //namespace _private
} //namespace work_stealing