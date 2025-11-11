#include "task_engine/task_engine.hpp"
#include "task_engine/resource_monitor.hpp"
#include "task_engine/task_preemptor.hpp"
#include <iostream>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== NexusTask Resource Management Demo ===\n\n";

    TaskExecutor executor(4);
    executor.start();

    std::cout << "1. Resource Limits\n";
    std::cout << "------------------\n";
    
    ResourceMonitor resource_monitor;
    
    // Task with time limit
    ResourceLimits time_limit;
    time_limit.max_execution_time = std::chrono::milliseconds(100);
    
    auto task1_id = executor.submit([]() -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 1;
    }, "LongRunningTask", TaskOptions{});
    
    resource_monitor.start_monitoring(task1_id, time_limit);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool exceeded = resource_monitor.check_limits(task1_id);
    std::cout << "After 50ms - Limits exceeded: " << (exceeded ? "No" : "No") << "\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    exceeded = resource_monitor.check_limits(task1_id);
    std::cout << "After 110ms - Limits exceeded: " << (exceeded ? "Yes" : "No") << "\n";
    
    auto usage = resource_monitor.get_usage(task1_id);
    std::cout << "Resource usage:\n";
    std::cout << "  Execution time: " << usage.execution_time.count() << " ms\n";
    std::cout << "  Memory: " << usage.memory_bytes << " bytes\n";
    std::cout << "  CPU: " << usage.cpu_percent << "%\n";
    
    resource_monitor.stop_monitoring(task1_id);

    std::cout << "\n2. Task Preemption\n";
    std::cout << "------------------\n";
    
    TaskPreemptor preemptor(executor);
    preemptor.set_enabled(true);
    
    // Submit low priority tasks
    for (int i = 0; i < 5; ++i) {
        executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return i;
        }, "LowPriority_" + std::to_string(i), 
        TaskOptions{.priority = TaskPriority::Low});
    }
    
    // Submit high priority task
    auto high_priority_id = executor.submit([]() -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 999;
    }, "HighPriority", TaskOptions{.priority = TaskPriority::High});
    
    preemptor.preempt_for(high_priority_id, TaskPriority::Normal);
    std::cout << "Active preemptions: " << preemptor.active_preemptions() << "\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    preemptor.cancel_preemption(high_priority_id);
    
    executor.wait_for_completion();
    executor.stop();
    
    std::cout << "\nDemo completed!\n";
    return 0;
}

