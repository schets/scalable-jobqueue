#ifndef MPMC_HPP_
#define MPMC_HPP_
#include <queue_util.hpp>
#include <memory>
#include <utility>
#include <cstdint>

//multi producer multi consumer queue
//this is built under the assumption that there will
//rarely be other consumers, but often other producers
//so no cas version here. rare spurious failure is ok here
//especially since it happens only when queue is very near full
//anyways

//could do some spmc multiplex thing
//but since contention isn't gonna be that high
//and wait free, I think this should be fine
//could check it out though

//queuesize is static since the code requires queuesize to be a power
//of two to function - data corruption otherwise, best not left as a
//runtime check
template<class T, size_t qsize>
class alignas(64) mpmc_queue {
public:

    static constexpr size_t modsize = qsize - 1;
    //!is power of two
    static_assert(((qsize != 0) && !(qsize & (qsize - 1))),
                  "qsize must be a power of two");


    std::unique_ptr<T[]> elements;
    char _c0[q_u::cache_size - sizeof(elements)];
    
    //producer side
    std::atomic<uint64_t> tail;
    std::atomic<uint64_t> enqueue_opt;
    std::atomic<uint64_t> enqueue_over;
    char _c1[q_u::cache_size -
             (sizeof(tail) + sizeof(enqueue_opt) + sizeof(enqueue_over))];

    //based on
    //https://github.com/cameron314/concurrentqueue/blob/master/concurrentqueue.h
    //really need to benchmark against the simple cas loop
    //this seems like a lot of extra work
    std::atomic<uint64_t> head, dequeue_opt, dequeue_over;

    template<class U>
    bool try_push (U&& data);
    bool try_pop (T& out);
    bool try_pop_commit (T& out);
    bool try_pop_dec (T& out);

    mpmc_queue()
        :
        elements(new T[qsize]),
        tail(0),
        head_cache(0),
        head(0),
        dequeue_opt(0),
        dequeue_over(0) {
        for (size_t i = 0; i < qsize; i++) {
            elements[i] = 0xdeadbeef;
        }
    }
};

//yay wait free mpmc queue
//now for testing...
//maybe benchmark other version as well
template<class T, size_t qsize>
template<class U>
bool mpmc_queue<T, qsize>::try_push(U&& datum) {
    uint64_t modsize = qsize - 1;

    auto enqueue = enqueue_opt.load(std::memory_order_relaxed);
    auto head = head.load(std::memory_order_relaxed);

    //don't double check here, move on somewhere else
    if ((enqueue - head) >= (qsize - 1))
        return false;

    enqueue = enqueue_opt.fetch_add(1, std::memory_order_acq_rel);
    
    if ((enqueue - head) >= (qsize - 1)) {
        head = head.load(std::memory_order_aquire);
        if ((enqueue - head) >= (qsize - 1)) {
            enqueue_opt.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
    }

    auto ctail = tail.fetch_add(1, std::memory_order_acq_rel);
    size_t ind = ctail & modsize;
    elements[ind] = std::forward<U>(datum);
    return true;
}

//trickier since multi consumers
//need to test in the actual queue
//low but existing contention, also
//unsure how lock prefix actually affects 
//other reads/writes
//cas has less locks than other two
//and under low contention, won't loop a whole lot
//worth keeping all for now as investigating
//but will set cas for now as it seems to generally have better
//low contention performance
//also since contention is low may not be an issue
//and doesn't have a spurious failure case,  where all contenders fail
//to dequeue even when at least one could
//but being wait free is nice


template<class T, size_t qsize>
bool mpmc_queue<T, qsize>::try_pop_commit(T& out) {
    uint64_t modsize = qsize - 1;
    //quick loads to avoid unneeded fences in empty case
    //try without?
    auto ctail = tail.load(std::memory_order_relaxed);
    auto dequeue = dequeue_opt.load(std::memory_order_relaxed);
    auto overcommit = dequeue_over.load(std::memory_order_relaxed);
    if ((dequeue - overcommit) >= ctail) {
        ctail = tail.load(std::memory_order_acquire);
        if ((dequeue - overcommit) >= ctail)
            return false;
    }
    
    //perform fence so that now, when we reload the optimistic count,
    //that load will be as recent (or more) than the overcommit load
    
    std::atomic_thread_fence(std::memory_order_acquire);

    //load is acquire since memory fence takes care of that
    dequeue = dequeue_opt.fetch_add(1, std::memory_order_relaxed);

    

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
    auto& el = elements[(size_t)(chead & modsize)];
    out = std::move(el);
    el.~T();
    return true;
}

//try the non overcommit queue - all these for eventual benchmarking
template<class T, size_t qsize>
bool mpmc_queue<T, qsize>::try_pop_dec(T& out) {
    uint64_t modsize = qsize - 1;
    //quick loads to avoid unneeded fences in empty case
    auto ctail = tail.load(std::memory_order_relaxed);
    auto dequeue = dequeue_opt.load(std::memory_order_relaxed);
    if (dequeue >= ctail)
        return false;
    //perform fence so that now, when we reload the optimistic count,
    //that load will be as recent (or more) than the overcommit load
    
    dequeue = dequeue_opt.fetch_add(1, std::memory_order_acq_rel);

    
    //reload tail to get more recent estimate

    //reload tail if need be
    if (dequeue >= ctail) {
        ctail = tail.load(std::memory_order_acquire);
        if (dequeue >= ctail) {
            //release ensures that the write occurs after the fetch_add on
            //the dequeue_opt
            dequeue_opt.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
    }

    auto chead = head.fetch_add(1, std::memory_order_acq_rel);
    auto& el = elements[(size_t)(chead & modsize)];
    out = std::move(el);
    el.~T();
    return true;
}


//with little contention, each one is close
template<class T, size_t qsize>
bool mpmc_queue<T, qsize>::try_pop(T& out) {
    uint64_t modsize = qsize - 1;
    //should work most of time, cas will save us anyways
    //if some spooky changes happen to head
    auto ctail = tail.load(std::memory_order_relaxed);
    auto chead = head.load(std::memory_order_relaxed);

    if (chead >= ctail) {
        return false;
    }

    //will almost always work
    //very little contention
    //but possible, so much use acq_rel ordering
    while (!head.compare_exchange_weak(chead,
                                       chead+1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {

        if (chead >= ctail) {
            //avoid reload unless needed
            ctail = tail.load(std::memory_order_acquire);
            if (chead >= ctail)
                return false;
        }

    }
    auto& el = elements[(size_t)(chead & modsize)];
    out = std::move(el);
    el.~T();
    return true;
}
#endif
