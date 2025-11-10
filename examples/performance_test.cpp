#include "task_engine/task_engine.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <thread>
#include <cstdint>

using namespace task_engine;

int main() {
    std::cout << "=== Task Engine Performance Test ===\n\n";
    
    constexpr size_t NUM_TASKS = 10000;
    constexpr size_t NUM_THREADS = 8;
    
    TaskExecutor executor(NUM_THREADS);
    executor.start();
    
    PerformanceMonitor monitor;
    
    std::cout << "Configuration:\n";
    std::cout << "  Tasks: " << NUM_TASKS << "\n";
    std::cout << "  Threads: " << NUM_THREADS << "\n\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "Submitting " << NUM_TASKS << " tasks...\n";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 10);
    
    std::vector<TaskId> task_ids;
    task_ids.reserve(NUM_TASKS);
    
    for (size_t i = 0; i < NUM_TASKS; ++i) {
        int work_amount = dis(gen);
        auto task_id = executor.submit([work_amount, &monitor]() -> TaskResult {
            auto task_start = std::chrono::steady_clock::now();
            
            // Simulate work
            volatile int sum = 0;
            for (int j = 0; j < work_amount * 1000; ++j) {
                sum += j;
            }
            
            auto task_end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                task_end - task_start).count();
            
            monitor.record_completion(duration);
            return sum;
        }, "PerfTask_" + std::to_string(i));
        
        task_ids.push_back(task_id);
        monitor.record_submission();
    }
    
    std::cout << "Waiting for completion...\n";
    executor.wait_for_completion();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    auto metrics = monitor.get_metrics();
    
    std::cout << "\n=== Results ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time: " << total_duration << " ms\n";
    std::cout << "Tasks submitted: " << metrics.tasks_submitted << "\n";
    std::cout << "Tasks completed: " << metrics.tasks_completed << "\n";
    std::cout << "Tasks failed: " << metrics.tasks_failed << "\n";
    std::cout << "Average execution time: " << monitor.average_execution_time() << " μs\n";
    std::cout << "Min execution time: " << metrics.min_execution_time_us << " μs\n";
    std::cout << "Max execution time: " << metrics.max_execution_time_us << " μs\n";
    std::cout << "Throughput: " << (NUM_TASKS * 1000.0 / total_duration) << " tasks/sec\n";
    
    executor.stop();
    
    return 0;
}
