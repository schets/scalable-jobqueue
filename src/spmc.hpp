#ifndef SPMC_HPP_
#define SPMC_HPP_
#include <queue_util.hpp>
#include <memory>
#include <utilities>
#include <cstdint>

//investigate unbounded version, from same implementation?
//single producer multi consumer queue
//this is built under the assumption that there will
//rarely be other consumers

//queuesize is static since the code requires queuesize to be a power
//of two to function - data corruption otherwise, best not left as a
//runtime check
template<class T, uint64_t queuesize>
class alignas(64) spmc_queue {

    static constexpr uint64_t modsize = queuesize - 1;
    //!is power of two
    static_assert(((qsize != 0) && !(qsize & (qsize - 1))),
                  "qsize must be a power of two");


    std::unique_ptr<T> elements;
    char _c0[q_u::cache_size - sizeof(elements)];
    
    //producer side
    std::atomic<uint64_t> tail;
    uint64_t head_cache;
    char _c1[q_u::cache_size - (sizeof(tail) + sizeof(head_c))];

    //based on
    //https://github.com/cameron314/concurrentqueue/blob/master/concurrentqueue.h
    //really need to benchmark against the simple cas loop
    //this seems like a lot of extra work
    std::atomic<uint64_t> head, dequeue_opt, dequeue_over;

public:
    template<class U>
    bool try_push (U&& data);
    bool try_pop (T& out);
    bool try_pop_cas (T& out);
};

template<class U>
template<class T>
bool spmc_queue<T>::try_push(U&& datum) {

    uint64_t ctail = tail.load(std::memory_order_relaxed);
    //according to the last seen value of head, no space left :(
    if ((ctail - head_cache) >= queuesize) {
        head_cache = head.load(std::memory_order_acquire);
        if ((ctail - head_cache) >= queuesize) {
            return false;
        }
    }

    elements[ctail & modsize] = std::forward<U>(datum);

    tail.store(ctail + 1, std::memory_order_release);
}

//trickier since multi consumers

template<class T>
bool spmc_queue<T>::try_pop(T& out) {
    //quick loads to avoid unneeded fences in empty case
    auto ctail = tail.load(std::memory_order_relaxed);
    auto overcommit = dequeue_over.load(std::memory_order_relaxed);
    auto dequeue = dequeue_opt.load(std::memory_order_relaxed);
    if ((dequeue - overcommit) >= ctail)
        return false;
    //perform fence so that now, when we reload the optimistic count,
    //that load will be as recent (or more) than the overcommit load
    std::atomic_thread_fence(std::memory_order_acquire);
    
    dequeue = dequeue_opt.fetch_add(1, std::memory_order_relaxed);

    //reload tail to get more recent estimate

    //reload tail if need be
    if ((dequeue - overcommit) >= ctail) {
        ctail = tail.load(std::memory_order_acquire);
    
        if ((dequeue - overcommit) >= ctail) {
            //release ensures that the write occurs after the fetch_add on
            //the dequeue_opt
            dequeue_over.fetch_add(1, std::memory_order_release);
            return false;
        }
    }

    auto chead = head.fetch_add(1, std::memory_order_acq_rel);
    auto& el = elements[chead & modsize];
    out = std::move(el);
    el.~T();
    return true;
}

//comparing cas to that 'effecient' hot mess of atomic reads and
//memory barriers. That's likely better in the face of serious
//contention, but this is being written assuming lower contention

template<class T, size_t qsize>
bool spmc_queue<T, qsize>::try_pop_cas(T& out) {
    //should work most of time, cas will save us anyways
    //if some spooky changes happen to head
    auto ctail = tail.load(std::memory_order_relaxed);
    auto chead = head.load(std::memory_order_relaxed);

    //could try atomic tail cache - will generally be accessed from
    //one thread only
    if (chead >= ctail) {
        return false;
    }

    //will almost always work
    //very little contention
    //but possible, so much use acq_rel ordering
    while (!head.compare_and_swap_weak(chead,
                                       chead+1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_aquire)) {

        chead = head.load(std::memory_order_relaxed);
        if (chead >= ctail) {
            //avoid reload unless needed, may call out to cache
            ctail = tail.load(std::memory_order_relaxed);
            if (chead >= ctail)
                return false;
        }

    }
    auto& el = elements[chead & modsize];
    out = std::move(el);
    el.~T();
    return true;
}
#endif