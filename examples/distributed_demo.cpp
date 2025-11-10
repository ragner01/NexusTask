#include "task_engine/task_engine.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== Distributed Task Executor Demo ===\n\n";
    
    // Create distributed executor
    DistributedExecutor dist_executor("localhost", 8080, 4);
    
    // Add remote nodes (simulated)
    dist_executor.add_node("192.168.1.100", 8080, 8);
    dist_executor.add_node("192.168.1.101", 8080, 8);
    dist_executor.add_node("192.168.1.102", 8080, 8);
    
    dist_executor.start();
    
    std::cout << "Submitting tasks to distributed executor...\n";
    
    std::vector<TaskId> task_ids;
    for (int i = 0; i < 20; ++i) {
        auto task_id = dist_executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cout << "Task " << i << " executed\n";
            return i * 2;
        }, "DistributedTask_" + std::to_string(i));
        
        task_ids.push_back(task_id);
    }
    
    std::cout << "\nWaiting for all tasks to complete...\n";
    dist_executor.wait_for_completion();
    
    std::cout << "\nAll distributed tasks completed!\n";
    
    dist_executor.stop();
    
    return 0;
}

