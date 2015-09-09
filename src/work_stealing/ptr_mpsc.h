#ifndef MPSC_HPP_
#define MPSC_HPP_

#include "../util/buffer_size.hpp"

#include <atomic>
#include <mutex>

//a stack might provide better cache benefits,
//but these shouldn't get too big
//and the stack has all sorts of things like
//suffering from contention and the aba problem

//based on https://github.com/mstump/queues/blob/master/include/mpsc-queue.hpp

//wait free mpsc queue used for transfering pointers
//back to original worker for deallocation
//isn't linearizable but doesn't need to be
//contentionless and effecient is more important

class ptr_mpsc {

public:
	void push(void *topush);
	void *pop();
};

#endif
