#include "task_engine/task_engine.hpp"
#include "task_engine/task_scheduler.hpp"
#include "task_engine/task_batch.hpp"
#include "task_engine/task_cache.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== NexusTask New Features Demo ===\n\n";

    TaskExecutor executor(4);
    executor.start();

    std::cout << "1. Task Scheduling (Delayed Execution)\n";
    std::cout << "--------------------------------------\n";
    
    TaskScheduler scheduler(executor);
    scheduler.start();

    // Schedule a task to run after 500ms
    scheduler.schedule_after([]() -> TaskResult {
        std::cout << "Delayed task executed!\n";
        return 42;
    }, std::chrono::milliseconds(500), "DelayedTask");

    // Schedule a periodic task
    scheduler.schedule_periodic([]() -> TaskResult {
        static int count = 0;
        std::cout << "Periodic task execution #" << ++count << "\n";
        return count;
    }, std::chrono::milliseconds(200), "PeriodicTask");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    scheduler.stop();

    std::cout << "\n2. Task Batching\n";
    std::cout << "----------------\n";

    TaskBatch batch(executor);
    for (int i = 0; i < 5; ++i) {
        batch.add([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i * i;
        }, "BatchTask_" + std::to_string(i));
    }
    
    std::cout << "Submitted " << batch.task_ids().size() << " tasks in batch\n";
    batch.wait();
    std::cout << "All batch tasks completed!\n";

    std::cout << "\n3. Result Caching\n";
    std::cout << "-----------------\n";

    TaskResultCache<std::string> cache(std::chrono::seconds(2));

    // Expensive computation
    auto compute = [](int n) -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return n * n;
    };

    // First call - cache miss
    auto key = "compute_42";
    auto cached = cache.get(key);
    if (!cached.has_value()) {
        std::cout << "Cache miss - computing...\n";
        auto result = compute(42);
        cache.put(key, result);
        cached = result;
    } else {
        std::cout << "Cache hit!\n";
    }
    std::cout << "Result: " << std::any_cast<int>(*cached) << "\n";

    // Second call - cache hit
    cached = cache.get(key);
    if (cached.has_value()) {
        std::cout << "Cache hit - using cached result!\n";
        std::cout << "Result: " << std::any_cast<int>(*cached) << "\n";
    }

    executor.stop();
    std::cout << "\nDemo completed!\n";
    return 0;
}

