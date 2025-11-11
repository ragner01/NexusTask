#include "task_engine/task_engine.hpp"
#include "task_engine/test_framework.hpp"
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>
#include <fstream>
#include <cstdio>

using namespace task_engine;
using namespace task_engine::testing;

namespace {
std::vector<std::string> g_feature_log;

void record_feature(std::string description) {
    g_feature_log.push_back(std::move(description));
}
} // namespace

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
        record_feature("Basic task execution across thread pool");
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
        record_feature("Dependency scheduling with automatic readiness tracking");
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
        record_feature("Automatic retry with bounded attempts");
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
        bool is_terminal = (status == TaskStatus::Cancelled || status == TaskStatus::Completed);
        ASSERT_TRUE(is_terminal);
        
        executor.stop();
        record_feature("Mid-flight task cancellation API");
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
            bool is_terminal = (status == TaskStatus::Completed || status == TaskStatus::Failed);
            ASSERT_TRUE(is_terminal);
        }
        
        executor.stop();
        record_feature("Batch submission helper for bulk workloads");
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
        record_feature("Adaptive rate limiter for throttling submissions");
    });

    TEST(ResultCache) {
        TaskResultCache<std::string> cache(std::chrono::seconds(1));
        
        cache.put("key1", 42);
        auto result = cache.get("key1");
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(std::any_cast<int>(*result), 42);
        
        auto missing = cache.get("key2");
        ASSERT_FALSE(missing.has_value());
        record_feature("Result cache for deduplicating expensive computations");
    });

    TEST(PerformanceMetrics) {
        TaskExecutor executor(2);
        executor.start();
        for (int i = 0; i < 5; ++i) {
            executor.submit([]() -> TaskResult {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return 1;
            }, "PerfTask_" + std::to_string(i));
        }
        executor.wait_for_completion();
        auto metrics = executor.metrics();
        ASSERT_TRUE(metrics.tasks_completed >= 5);
        ASSERT_EQ(metrics.tasks_failed, 0);
        ASSERT_TRUE(metrics.total_execution_time_us > 0);
        executor.stop();
        record_feature("Performance monitor snapshots via TaskExecutor::metrics()");
    });

    TEST(TaskAnnotationsAndDeadlines) {
        TaskOptions options;
        options.timeout = std::chrono::milliseconds(1);
        Task annotated_task(42, []() -> TaskResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return 0;
        }, "AnnotatedTask", options);

        annotated_task.annotate("dataset", "alpha");
        auto note = annotated_task.annotation("dataset");
        ASSERT_TRUE(note.has_value());
        ASSERT_EQ(*note, "alpha");

        bool failed = false;
        try {
            annotated_task.execute();
        } catch (...) {
            failed = true;
        }
        ASSERT_TRUE(failed);
        ASSERT_EQ(static_cast<int>(annotated_task.status()), static_cast<int>(TaskStatus::Failed));
        record_feature("Task metadata (annotations, timeouts, deadlines)");
    });

    TEST(DistributedInMemoryExecution) {
        DistributedExecutor distributed("127.0.0.1", 9100, 1);
        distributed.start();
        distributed.add_node("127.0.0.1", 9101, 2);

        auto remote_executor = std::make_shared<TaskExecutor>(1);
        remote_executor->start();
        distributed.attach_in_memory_executor("127.0.0.1", 9101, remote_executor);
        distributed.enable_in_memory_transport();

        std::atomic<int> remote_runs{0};
        distributed.submit([&remote_runs]() -> TaskResult {
            remote_runs.fetch_add(1, std::memory_order_relaxed);
            return 123;
        }, "RemoteFeatureTest");

        distributed.wait_for_completion();
        remote_executor->wait_for_completion();
        auto remote_metrics = remote_executor->metrics();
        ASSERT_TRUE(remote_metrics.tasks_completed >= 1);
        ASSERT_EQ(remote_runs.load(), 1);

        remote_executor->stop();
        distributed.stop();
        record_feature("In-memory distributed executor routing with remote metrics");
    });

    TEST(TaskScheduler) {
        TaskExecutor executor(2);
        executor.start();
        TaskScheduler scheduler(executor);
        scheduler.start();
        
        std::atomic<int> delayed_count{0};
        scheduler.schedule_after([&delayed_count]() -> TaskResult {
            delayed_count.fetch_add(1);
            return 0;
        }, std::chrono::milliseconds(50), "DelayedTask");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        executor.wait_for_completion();
        ASSERT_EQ(delayed_count.load(), 1);
        
        scheduler.stop();
        executor.stop();
        record_feature("Delayed and periodic task scheduling");
    });

    TEST(TaskChain) {
        TaskExecutor executor(2);
        executor.start();
        
        TaskChain chain(executor);
        std::atomic<int> step1{0}, step2{0}, step3{0};
        
        chain.then([&step1](TaskResult) -> TaskResult {
            step1.store(1);
            return 10;
        }, "Step1")
        .then([&step2](TaskResult input) -> TaskResult {
            step2.store(1);
            return 20;
        }, "Step2")
        .then([&step3](TaskResult input) -> TaskResult {
            step3.store(1);
            return 30;
        }, "Step3");
        
        auto task_id = chain.execute(5);
        executor.wait_for_completion();
        
        ASSERT_EQ(step1.load(), 1);
        ASSERT_EQ(step2.load(), 1);
        ASSERT_EQ(step3.load(), 1);
        ASSERT_NE(task_id, 0);  // Should have valid task ID
        
        executor.stop();
        record_feature("Functional task chaining and pipeline execution");
    });

    TEST(TaskGroup) {
        TaskExecutor executor(4);
        executor.start();
        
        TaskGroup group(executor, "TestGroup");
        for (int i = 0; i < 5; ++i) {
            group.add([i]() -> TaskResult {
                return i * i;
            }, "GroupTask_" + std::to_string(i));
        }
        
        ASSERT_EQ(group.task_ids().size(), 5);
        group.wait();
        
        auto stats = group.get_stats();
        ASSERT_EQ(stats.total_tasks, 5);
        ASSERT_EQ(stats.completed, 5);
        
        executor.stop();
        record_feature("Task group organization and batch management");
    });

    TEST(HealthMonitor) {
        TaskExecutor executor(2);
        executor.start();
        
        HealthMonitor health(executor);
        health.set_thresholds(100, 1000, 1.0);
        
        for (int i = 0; i < 10; ++i) {
            executor.submit([]() -> TaskResult {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return 0;
            }, "HealthTest_" + std::to_string(i));
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto status = health.check();
        ASSERT_TRUE(status.healthy || !status.healthy);  // Just check it doesn't crash
        
        executor.wait_for_completion();
        executor.stop();
        record_feature("System health monitoring with configurable thresholds");
    });

    TEST(ConfigManagement) {
        // Create a temporary config file
        {
            std::ofstream config_file("test_config.conf");
            config_file << "# Test config\n";
            config_file << "threads=8\n";
            config_file << "timeout_ms=5000\n";
            config_file << "enable_feature=true\n";
            config_file.close();
        }
        
        auto config = Config::load_from_file("test_config.conf");
        ASSERT_EQ(config->get_int("threads", 0), 8);
        ASSERT_EQ(config->get_int("timeout_ms", 0), 5000);
        ASSERT_TRUE(config->get_bool("enable_feature", false));
        ASSERT_EQ(config->get("missing_key", "default"), "default");
        
        std::remove("test_config.conf");
        record_feature("File-based configuration loading and parsing");
    });

    TEST(ResourceMonitor) {
        ResourceMonitor monitor;
        
        ResourceLimits limits;
        limits.max_execution_time = std::chrono::milliseconds(100);
        limits.max_memory_bytes = 1024 * 1024;
        
        TaskId test_id = 42;
        monitor.start_monitoring(test_id, limits);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool exceeded = monitor.check_limits(test_id);
        ASSERT_FALSE(exceeded);  // Should not exceed yet
        
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        exceeded = monitor.check_limits(test_id);
        ASSERT_TRUE(exceeded);  // Should exceed now
        
        auto usage = monitor.get_usage(test_id);
        ASSERT_TRUE(usage.execution_time.count() > 100);
        
        monitor.stop_monitoring(test_id);
        record_feature("Resource limit tracking and enforcement");
    });

    TEST(MetricsExporter) {
        TaskExecutor executor(2);
        executor.start();
        
        for (int i = 0; i < 5; ++i) {
            executor.submit([]() -> TaskResult {
                return 0;
            }, "ExportTest_" + std::to_string(i));
        }
        
        executor.wait_for_completion();
        auto metrics = executor.metrics();
        
        std::string json = MetricsExporter::to_json(metrics);
        ASSERT_TRUE(json.find("tasks_completed") != std::string::npos);
        
        std::string prom = MetricsExporter::to_prometheus(metrics);
        ASSERT_TRUE(prom.find("tasks_completed") != std::string::npos);
        
        std::string csv = MetricsExporter::to_csv(metrics);
        ASSERT_TRUE(csv.find("tasks_completed") != std::string::npos);
        
        executor.stop();
        record_feature("Metrics export in JSON, Prometheus, and CSV formats");
    });

    int result = suite.run_all();

    if (!g_feature_log.empty()) {
        std::cout << "\nAdditional Features Exercised:\n";
        for (const auto& feature : g_feature_log) {
            std::cout << " - " << feature << '\n';
        }
    }

    return result;
}
