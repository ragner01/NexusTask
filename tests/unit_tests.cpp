#include "task_engine/task_engine.hpp"
#include "task_engine/test_framework.hpp"
#include <thread>
#include <chrono>

using namespace task_engine;
using namespace task_engine::testing;

int main() {
    TestSuite suite;

    TEST(BasicTaskExecution) {
        TaskExecutor executor(2);
        executor.start();
        
        std::atomic<int> result{0};
        auto task_id = executor.submit([&result]() -> TaskResult {
            result.store(42, std::memory_order_release);
            return 42;
        }, "TestTask");
        
        executor.wait_for_completion();
        ASSERT_EQ(result.load(std::memory_order_acquire), 42);
        
        executor.stop();
    });

    TEST(TaskDependencies) {
        TaskExecutor executor(2);
        executor.start();
        
        std::atomic<int> step1_done{0};
        std::atomic<int> step2_done{0};
        
        auto task1 = executor.submit([&step1_done]() -> TaskResult {
            step1_done.store(1, std::memory_order_release);
            return 10;
        }, "Step1");
        
        auto task2 = executor.submit_with_dependencies(
            [&step2_done]() -> TaskResult {
                step2_done.store(1, std::memory_order_release);
                return 20;
            },
            {task1},
            "Step2"
        );
        
        executor.wait_for_completion();
        ASSERT_EQ(step1_done.load(), 1);
        ASSERT_EQ(step2_done.load(), 1);
        
        executor.stop();
    });

    TEST(TaskRetry) {
        TaskExecutor executor(2);
        executor.start();
        
        std::atomic<int> attempts{0};
        TaskOptions options;
        options.max_retries = 2;
        
        executor.submit([&attempts]() -> TaskResult {
            int attempt = attempts.fetch_add(1) + 1;
            if (attempt < 3) {
                throw std::runtime_error("Fail");
            }
            return attempt;
        }, "RetryTask", options);
        
        executor.wait_for_completion();
        ASSERT_EQ(attempts.load(), 3);  // Initial + 2 retries
        
        executor.stop();
    });

    TEST(TaskCancellation) {
        TaskExecutor executor(2);
        executor.start();
        
        auto task_id = executor.submit([]() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return 0;
        }, "CancellableTask");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool cancelled = executor.cancel(task_id);
        ASSERT_TRUE(cancelled);
        
        executor.wait_for_completion();
        auto status = executor.get_task_status(task_id);
        ASSERT_TRUE(status == TaskStatus::Cancelled || status == TaskStatus::Completed);
        
        executor.stop();
    });

    TEST(TaskBatch) {
        TaskExecutor executor(4);
        executor.start();
        
        TaskBatch batch(executor);
        for (int i = 0; i < 10; ++i) {
            batch.add([i]() -> TaskResult {
                return i * i;
            }, "BatchTask_" + std::to_string(i));
        }
        
        ASSERT_EQ(batch.task_ids().size(), 10);
        batch.wait();
        
        auto statuses = batch.get_statuses();
        for (auto status : statuses) {
            ASSERT_TRUE(status == TaskStatus::Completed || status == TaskStatus::Failed);
        }
        
        executor.stop();
    });

    TEST(RateLimiter) {
        RateLimiter limiter({5, std::chrono::seconds(1), std::chrono::milliseconds(200)});
        
        int allowed = 0;
        for (int i = 0; i < 10; ++i) {
            if (limiter.try_acquire()) {
                ++allowed;
            }
        }
        
        ASSERT_TRUE(allowed <= 5);  // Should be rate limited
    });

    TEST(ResultCache) {
        TaskResultCache<std::string> cache(std::chrono::seconds(1));
        
        cache.put("key1", 42);
        auto result = cache.get("key1");
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(std::any_cast<int>(*result), 42);
        
        auto missing = cache.get("key2");
        ASSERT_FALSE(missing.has_value());
    });

    return suite.run_all();
}

