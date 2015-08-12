#ifndef WORK_QUEUE_HPP_
#define WORK_QUEUE_HPP_

#include <spmc.hpp>
#include <thread>
#include <functional>

//Uses a single spmc queue per thread
//but upon failure to get work
//a thread goes and looks for other threads
//while possible to have a single spmc queue
//especially since the queue is wait free
//costs will be paid in the form of cache contention
//on pretty much every lookup
//now
//in the real world
//that's a-ok since work done by threads is going to be longer than
//the cost of a memory read. But it's more interesting to make these
//things highly optimized

template<size_t qsize = 4096>
class sp_work_queue {

    typedef std::function<void()> job;
    
    struct work_queue {
        spmc_queue<job, qsize> queue;
        uint64_t id;
        uint64_t _counter; //used to make work stealing more 'random'
    };

    std::atomic<uint64_t> recent_work_stole;
    
    size_t n_queues;
    size_t current_queue;
    std::unique_ptr<spmc_queue<T, qsize>> queues;
    
    bool add_job(job&& jb);
    bool add_job(const job& jb);
};

template<size_t qsize>
bool sp_work_queue::add_job(const job& jb) {
    for (size_t tries = 0; tries < n_queues * 2; tries++) {
        auto& curq = queues[current_queue];
        current_queue = (current_queue + 1) % n_queues;
        if (curq.try_push(jb))
            return true;
    }
    return false;
}

#endif
