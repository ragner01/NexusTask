#include "task_engine/task_engine.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== Task Engine Demo ===\n\n";
    
    // Create executor with 4 threads
    TaskExecutor executor(4);
    executor.start();
    
    std::cout << "1. Simple Task Execution\n";
    std::cout << "------------------------\n";
    
    // Submit simple tasks
    std::vector<TaskId> task_ids;
    for (int i = 0; i < 10; ++i) {
        auto task_id = executor.submit([i]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return i * i;
        }, "SquareTask_" + std::to_string(i));
        task_ids.push_back(task_id);
    }
    
    executor.wait_for_completion();
    std::cout << "All simple tasks completed!\n\n";
    
    std::cout << "2. Task Dependencies\n";
    std::cout << "--------------------\n";
    
    // Create tasks with dependencies
    auto task1 = executor.submit([]() -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "Task 1: Computing initial value...\n";
        return 42;
    }, "Task1");
    
    auto task2 = executor.submit([]() -> TaskResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "Task 2: Computing initial value...\n";
        return 10;
    }, "Task2");
    
    // Task 3 depends on task1 and task2
    auto task3 = executor.submit_with_dependencies(
        []() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "Task 3: Combining results...\n";
            return 52;  // Would use results from task1 and task2
        },
        {task1, task2},
        "Task3_Dependent"
    );
    
    executor.wait_for_completion();
    std::cout << "All dependent tasks completed!\n\n";
    
    std::cout << "3. Complex Workflow\n";
    std::cout << "-------------------\n";
    
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
    std::cout << "Complex workflow completed!\n\n";
    
    executor.stop();
    
    std::cout << "Demo completed successfully!\n";
    return 0;
}

