#include "common/lockfree.hpp"
#include "common/sync.hpp"

#include <thread>
#include <iostream>
#include <chrono>
#include <random>
#include <cstring>
#include <cstdlib>

constexpr bool multi_producer = true;
constexpr size_t num_push_threads = 3;
constexpr double duration = 1.0;
constexpr size_t max_ops_per_loop = 10;
constexpr size_t max_yield_count = 100;
constexpr size_t max_node_count = 100'000'000;

std::atomic<size_t> total_op_count{0};
std::atomic<ptrdiff_t> total_node_balance{0};
std::atomic<ptrdiff_t> total_sum{0};
std::atomic<bool> quit_pop_thread{false};

using queue_type = aoo::lockfree::concurrent_queue<int64_t, multi_producer>;

void push_thread_function(int num, queue_type& queue) {
    std::cout << "start push thread #" << num << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<uint32_t> iter_dist(1, max_ops_per_loop);
    std::uniform_int_distribution<uint32_t> yield_dist(0, max_yield_count);
    std::uniform_int_distribution<int64_t> value_dist(0, 100);

    using seconds = std::chrono::duration<double>;

    size_t local_sum = 0;
    size_t local_op_count = 0;
    size_t iter_count = 0;

    auto start = std::chrono::high_resolution_clock::now();
    for (;;) {
        ptrdiff_t count = 0;
        // push nodes
        if (total_node_balance.load(std::memory_order_relaxed) < max_node_count) {
            auto n = iter_dist(gen);
            for (size_t i = 0; i < n; ++i) {
                auto value = value_dist(gen);
                queue.push(value);
                count++;
                local_sum += value;
                local_op_count++;
            }
        }
        // yield in a loop
        auto k = yield_dist(gen);
        while (k--) {
            aoo::sync::pause_cpu();
        }

        iter_count++;

        total_node_balance.fetch_add(count);

        // check current time
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = seconds(now - start).count();
        if (elapsed >= duration) {
            break;
        }
    }

    total_sum += local_sum;
    total_op_count += local_op_count;

    std::cout << "quit push thread #" << num << " after "
              << local_op_count << " operations " << std::endl;
}

void pop_thread_function(queue_type& queue) {
    std::cout << "start pop thread" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()

    size_t local_sum = 0;
    size_t local_op_count = 0;

    while (!quit_pop_thread.load(std::memory_order_relaxed)) {
        // pop nodes
        int64_t value;
        if (queue.pop(value)) {
            local_sum += value;
            local_op_count++;
            total_node_balance--;
        } else {
            std::this_thread::yield();
        }
    }

    total_sum -= local_sum;
    total_op_count += local_op_count;

    std::cout << "quit pop thread after " << local_op_count
              << " operations" << std::endl;
}

int main(int argc, char *argv[]) {
    std::cout << "start " << num_push_threads << " push threads" << std::endl;
    std::cout << "---" << std::endl;

    queue_type queue;

    std::vector<std::thread> push_threads;
    auto num_threads = multi_producer ? num_push_threads : 1;
    for (size_t i = 0; i < num_threads; ++i) {
        auto thread = std::thread(push_thread_function, i+1, std::ref(queue));
        push_threads.emplace_back(std::move(thread));
    }

    std::cout << "---" << std::endl;

    std::thread pop_thread(pop_thread_function, std::ref(queue));

    for (auto& thread : push_threads) {
        thread.join();
    }
    std::cout << "joined push threads" << std::endl;
    std::cout << "---" << std::endl;

    quit_pop_thread.store(true);

    pop_thread.join();

    std::cout << "joined pop thread" << std::endl;
    std::cout << "---" << std::endl;

    std::cout << "total num. operations: " << total_op_count << std::endl;
    std::cout << "node balance: " << total_node_balance << std::endl;
    std::cout << "total sum: " << total_sum << std::endl;
    std::cout << "---" << std::endl;
    std::cout << "done!" << std::endl;

    assert(total_node_balance == 0);
    assert(total_sum == 0);

    return EXIT_SUCCESS;
}
