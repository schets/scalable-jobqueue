#include <thread>
#include "spmc.hpp"
#include <iostream>

std::atomic<int64_t> check_counter;

const int64_t num_iter = (int64_t)1e7; //check_counter should equal num_iter after all

constexpr size_t nthread = 1;

std::thread threads[nthread];

std::atomic<bool> isover;


void read_from_queue(spmc_queue<int64_t, 4096>* queue) {
    int64_t current = 1;
    while (current) {
        //lol contention
        while (!queue->try_pop(current)) {}
    }
}

void push_to_queue(spmc_queue<int64_t, 4096>* queue) {
    for (size_t i = 0; i < num_iter; i++) {
        while (!queue->try_push(1)) {}
    }
    for (const auto& th : threads)
        while (!queue->try_push(0)) {}
    std::cout << "pushed\n";
}

int _main() {
    check_counter.store(0);
    isover.store(false);
    
    spmc_queue<int64_t, 4096> global_queue;
    auto pushthread = std::thread(push_to_queue, &global_queue);
    for (auto& th : threads) {
        th = std::thread(read_from_queue, &global_queue);
    }

    //start/stop

    pushthread.join();
    for (auto& th : threads)
        th.join();
	return 0;
}

int main(void) {
    for(size_t i = 0; i < 5; i++) _main();
}
