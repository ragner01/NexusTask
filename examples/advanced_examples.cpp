#include "task_engine/task_engine.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>

using namespace task_engine;

int main() {
    std::cout << "=== NexusTask Advanced Examples ===\n\n";
    
    TaskExecutor executor(8);
    executor.start();
    
    std::cout << "1. Priority-Based Scheduling\n";
    std::cout << "-----------------------------\n";
    
    // Submit tasks with different priorities
    for (int i = 0; i < 5; ++i) {
        TaskOptions low_priority;
        low_priority.priority = TaskPriority::Low;
        executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i;
        }, "LowPriority_" + std::to_string(i), low_priority);
    }
    
    for (int i = 0; i < 5; ++i) {
        TaskOptions high_priority;
        high_priority.priority = TaskPriority::High;
        executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i + 100;
        }, "HighPriority_" + std::to_string(i), high_priority);
    }
    
    TaskOptions critical;
    critical.priority = TaskPriority::Critical;
    executor.submit([]() -> TaskResult {
        std::cout << "Critical task executed first!\n";
        return 999;
    }, "CriticalTask", critical);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "\n2. Task Retry Mechanism\n";
    std::cout << "----------------------\n";
    
    std::atomic<int> attempts{0};
    TaskOptions retry_options;
    retry_options.max_retries = 3;
    retry_options.priority = TaskPriority::High;
    
    executor.submit([&attempts]() -> TaskResult {
        int attempt = attempts.fetch_add(1) + 1;
        std::cout << "Attempt " << attempt << "\n";
        if (attempt < 3) {
            throw std::runtime_error("Simulated failure");
        }
        std::cout << "Success on attempt " << attempt << "\n";
        return attempt;
    }, "RetryTask", retry_options);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::cout << "\n3. Task Cancellation\n";
    std::cout << "--------------------\n";
    
    auto long_task = executor.submit([]() -> TaskResult {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cout << "Long task step " << i << "\n";
        }
        return 0;
    }, "LongTask");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    bool cancelled = executor.cancel(long_task);
    std::cout << "Task cancelled: " << (cancelled ? "Yes" : "No") << "\n";
    
    std::cout << "\n4. Timeout Handling\n";
    std::cout << "-------------------\n";
    
    TaskOptions timeout_options;
    timeout_options.timeout = std::chrono::milliseconds(50);
    
    executor.submit([]() -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 0;
    }, "TimeoutTask", timeout_options, [](TaskResult) {
        std::cout << "Task completed (should timeout)\n";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "\n5. Complex Dependency Graph\n";
    std::cout << "---------------------------\n";
    
    // Create a pipeline: A -> B -> C, and A -> D -> E
    auto A = executor.submit([]() -> TaskResult {
        std::cout << "Stage A: Data preparation\n";
        return 100;
    }, "StageA");
    
    auto B = executor.submit_with_dependencies(
        []() -> TaskResult {
            std::cout << "Stage B: Processing branch 1\n";
            return 200;
        },
        {A},
        "StageB"
    );
    
    auto C = executor.submit_with_dependencies(
        []() -> TaskResult {
            std::cout << "Stage C: Finalizing branch 1\n";
            return 300;
        },
        {B},
        "StageC"
    );
    
    auto D = executor.submit_with_dependencies(
        []() -> TaskResult {
            std::cout << "Stage D: Processing branch 2\n";
            return 400;
        },
        {A},
        "StageD"
    );
    
    auto E = executor.submit_with_dependencies(
        []() -> TaskResult {
            std::cout << "Stage E: Finalizing branch 2\n";
            return 500;
        },
        {D},
        "StageE"
    );
    
    executor.wait_for_completion();
    
    std::cout << "\n6. Performance Metrics\n";
    std::cout << "----------------------\n";
    
    auto monitor = executor.monitor();
    auto metrics = monitor->get_metrics();
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Tasks submitted: " << metrics.tasks_submitted << "\n";
    std::cout << "Tasks completed: " << metrics.tasks_completed << "\n";
    std::cout << "Tasks failed: " << metrics.tasks_failed << "\n";
    std::cout << "Average execution time: " 
              << monitor->average_execution_time() << " μs\n";
    std::cout << "Throughput: " 
              << monitor->throughput_per_second() << " tasks/sec\n";
    std::cout << "Peak inflight tasks: " << monitor->peak_inflight() << "\n";
    
    executor.stop();
    
    std::cout << "\nAll examples completed!\n";
    return 0;
}

