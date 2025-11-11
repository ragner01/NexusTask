#include "task_engine/task_engine.hpp"
#include "task_engine/task_group.hpp"
#include "task_engine/health_monitor.hpp"
#include "task_engine/config.hpp"
#include <iostream>
#include <fstream>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== NexusTask Advanced Features Demo ===\n\n";

    std::cout << "1. Configuration Management\n";
    std::cout << "----------------------------\n";
    
    // Create a sample config file
    {
        std::ofstream config_file("nexustask.conf");
        config_file << "# NexusTask Configuration\n";
        config_file << "threads=8\n";
        config_file << "max_tasks=10000\n";
        config_file << "enable_monitoring=true\n";
        config_file << "timeout_ms=5000\n";
        config_file.close();
    }
    
    auto config = Config::load_from_file("nexustask.conf");
    std::cout << "Loaded config:\n";
    std::cout << "  Threads: " << config->get_int("threads", 4) << "\n";
    std::cout << "  Max tasks: " << config->get_int("max_tasks", 1000) << "\n";
    std::cout << "  Monitoring: " << (config->get_bool("enable_monitoring") ? "enabled" : "disabled") << "\n";
    std::cout << "  Timeout: " << config->get_int("timeout_ms", 1000) << " ms\n";

    int num_threads = config->get_int("threads", 4);
    TaskExecutor executor(num_threads);
    executor.start();

    std::cout << "\n2. Task Groups\n";
    std::cout << "--------------\n";
    
    TaskGroup group1(executor, "DataProcessing");
    TaskGroup group2(executor, "Validation");
    
    // Add tasks to group1
    for (int i = 0; i < 5; ++i) {
        group1.add([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i * 2;
        }, "Process_" + std::to_string(i));
    }
    
    // Add tasks to group2
    for (int i = 0; i < 3; ++i) {
        group2.add([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i + 100;
        }, "Validate_" + std::to_string(i));
    }
    
    std::cout << "Group1 tasks: " << group1.task_ids().size() << "\n";
    std::cout << "Group2 tasks: " << group2.task_ids().size() << "\n";
    
    group1.wait();
    auto stats1 = group1.get_stats();
    std::cout << "Group1 stats - Completed: " << stats1.completed 
              << ", Failed: " << stats1.failed << "\n";

    std::cout << "\n3. Health Monitoring\n";
    std::cout << "--------------------\n";
    
    HealthMonitor health(executor);
    health.set_thresholds(100, 1000, 10.0);
    
    // Submit some tasks
    for (int i = 0; i < 50; ++i) {
        executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return i;
        }, "HealthTest_" + std::to_string(i));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto health_status = health.check();
    std::cout << "Health Status: " << (health_status.healthy ? "HEALTHY" : "UNHEALTHY") << "\n";
    std::cout << "  Message: " << health_status.status_message << "\n";
    std::cout << "  Active tasks: " << health_status.active_tasks << "\n";
    std::cout << "  Pending tasks: " << health_status.pending_tasks << "\n";
    std::cout << "  Throughput: " << health_status.throughput << " tasks/sec\n";
    std::cout << "  Avg latency: " << health_status.avg_latency_us << " μs\n";

    executor.wait_for_completion();
    executor.stop();
    
    // Cleanup
    std::remove("nexustask.conf");
    
    std::cout << "\nDemo completed!\n";
    return 0;
}

