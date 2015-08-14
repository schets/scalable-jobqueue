#ifndef WORK_QUEUE_HPP_
#define WORK_QUEUE_HPP_

#include <spmc.hpp>
#include <thread>
#include <functional>

//Uses a single queue per thread
//but upon failure to get work
//a thread goes and looks for other threads
//while possible to have a single wait free queue
//will pay the costs of contention. Or at least we will see...
///now
//in the real world
//that's a-ok since work done by threads is going to be longer than
//the cost of a memory read. But it's more interesting to make these
//things highly optimized

//avoiding virtual calls for now, is it worth it?
//would make simpler, indirect call prediction would help
//will benchmark this version first, and unless this is < 15-20
//ns/push-pop, will try virtualizing it

template<class Callable, template<typename> class Queue>
class work_queue {

    struct work_queue {
        Queue<Callable> queue;
        uint64_t id;
        uint64_t _counter; //used to make work stealing more 'random'
    };

    std::atomic<uint64_t> recent_work_stole;
    
    size_t n_queues;
    size_t current_queue;
    std::unique_ptr<Queue<Callable>> queues;
    bool add_job(const Callable& jb);
};

template<class Callable, template<typename> class Queue>
bool work_queue<Callable, Queue>::add_job(const job& jb) {
    for (size_t tries = 0; tries < n_queues * 2; tries++) {
        auto& curq = queues[current_queue];
        current_queue = (current_queue + 1) % n_queues;
        if (curq.try_push(jb))
            return true;
    }
    return false;
}

#endif
