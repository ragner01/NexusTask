#include "task_engine/task_engine.hpp"
#include "task_engine/rate_limiter.hpp"
#include "task_engine/task_chain.hpp"
#include "task_engine/metrics_exporter.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace task_engine;

int main() {
    std::cout << "=== NexusTask Additional Features Demo ===\n\n";

    TaskExecutor executor(4);
    executor.start();

    std::cout << "1. Rate Limiting\n";
    std::cout << "----------------\n";
    
    RateLimiter limiter({10, std::chrono::seconds(1), std::chrono::milliseconds(100)});
    limiter.set_limit("api", {5, std::chrono::seconds(1), std::chrono::milliseconds(200)});
    
    std::cout << "Submitting tasks with rate limiting...\n";
    int submitted = 0;
    int allowed = 0;
    
    for (int i = 0; i < 20; ++i) {
        if (limiter.try_acquire("api")) {
            executor.submit([i]() -> TaskResult {
                return i;
            }, "RateLimited_" + std::to_string(i));
            ++allowed;
        } else {
            std::cout << "Rate limited task " << i << "\n";
        }
        ++submitted;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "Submitted: " << submitted << ", Allowed: " << allowed << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n2. Task Chaining\n";
    std::cout << "----------------\n";
    
    TaskChain chain(executor);
    chain.then([](TaskResult input) -> TaskResult {
        int value = input.has_value() ? std::any_cast<int>(input) : 10;
        std::cout << "Step 1: Processing " << value << "\n";
        return value * 2;
    }, "Step1")
    .then([](TaskResult input) -> TaskResult {
        int value = std::any_cast<int>(input);
        std::cout << "Step 2: Processing " << value << "\n";
        return value + 5;
    }, "Step2")
    .then([](TaskResult input) -> TaskResult {
        int value = std::any_cast<int>(input);
        std::cout << "Step 3: Final result " << value << "\n";
        return value;
    }, "Step3");
    
    chain.execute();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\n3. Metrics Export\n";
    std::cout << "-----------------\n";
    
    auto metrics = executor.monitor()->get_metrics();
    
    std::cout << "JSON Format:\n";
    std::cout << MetricsExporter::to_json(metrics) << "\n\n";
    
    std::cout << "Prometheus Format:\n";
    std::cout << MetricsExporter::to_prometheus(metrics) << "\n\n";
    
    std::cout << "CSV Format:\n";
    std::cout << MetricsExporter::to_csv(metrics) << "\n";

    executor.wait_for_completion();
    executor.stop();
    
    std::cout << "\nDemo completed!\n";
    return 0;
}

