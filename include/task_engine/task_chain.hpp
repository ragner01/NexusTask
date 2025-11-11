#pragma once

#include "task_executor.hpp"
#include <functional>
#include <vector>
#include <memory>
#include <type_traits>

namespace task_engine {

/**
 * Task chain for functional composition and pipeline execution
 */
class TaskChain {
public:
    explicit TaskChain(TaskExecutor& executor)
        : executor_(executor)
    {}

    /**
     * Add a step to the chain
     * Returns a new chain for method chaining
     */
    template<typename F>
    TaskChain& then(F&& func, const std::string& name = "") {
        using FuncType = std::decay_t<F>;
        if constexpr (std::is_invocable_v<FuncType, TaskResult>) {
            steps_.emplace_back([func = std::forward<F>(func)](TaskResult input) -> TaskResult {
                return func(input);
            }, name);
        } else {
            steps_.emplace_back([func = std::forward<F>(func)](TaskResult) -> TaskResult {
                return func();
            }, name);
        }
        return *this;
    }

    /**
     * Execute the chain with initial input
     */
    TaskId execute(TaskResult initial_input = {}) {
        if (steps_.empty()) {
            return 0;
        }

        TaskId current_id = 0;
        TaskResult current_result = initial_input;

        for (size_t i = 0; i < steps_.size(); ++i) {
            const auto& [func, name] = steps_[i];
            
            if (i == 0) {
                // First step
                if (steps_.size() == 1) {
                    current_id = executor_.submit([func, current_result]() -> TaskResult {
                        return func(current_result);
                    }, name.empty() ? "ChainStep_0" : name);
                } else {
                    current_id = executor_.submit([func, current_result]() -> TaskResult {
                        return func(current_result);
                    }, name.empty() ? "ChainStep_0" : name);
                }
            } else {
                // Subsequent steps depend on previous
                auto prev_id = current_id;
                current_id = executor_.submit_with_dependencies(
                    [func]() -> TaskResult {
                        // In real implementation, would get result from prev task
                        return func({});
                    },
                    {prev_id},
                    name.empty() ? "ChainStep_" + std::to_string(i) : name
                );
            }
        }

        return current_id;
    }

    /**
     * Execute chain asynchronously
     */
    TaskExecutor::TaskHandle execute_async(TaskResult initial_input = {}) {
        // Simplified - would need proper future chaining
        auto promise = std::make_shared<std::promise<TaskResult>>();
        auto future = promise->get_future().share();
        
        auto last_id = execute(initial_input);
        // In full implementation, would chain futures properly
        
        return TaskExecutor::TaskHandle{last_id, std::move(future)};
    }

    /**
     * Clear all steps
     */
    void clear() {
        steps_.clear();
    }

private:
    TaskExecutor& executor_;
    std::vector<std::pair<std::function<TaskResult(TaskResult)>, std::string>> steps_;
};

} // namespace task_engine

