#pragma once

#include "task_executor.hpp"
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <thread>

namespace task_engine {

/**
 * Rate limiter using token bucket algorithm
 * Limits task submission rate per category
 */
class RateLimiter {
public:
    struct RateLimit {
        size_t max_tokens;
        std::chrono::milliseconds refill_interval;
        std::chrono::milliseconds token_refill_amount;
    };

    explicit RateLimiter(RateLimit default_limit = {100, std::chrono::seconds(1), std::chrono::milliseconds(100)})
        : default_limit_(default_limit)
    {}

    /**
     * Set rate limit for a category
     */
    void set_limit(const std::string& category, RateLimit limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        limits_[category] = limit;
        TokenBucket bucket;
        bucket.limit = limit;
        bucket.tokens = limit.max_tokens;
        bucket.last_refill = std::chrono::steady_clock::now();
        buckets_[category] = bucket;
    }

    /**
     * Try to acquire a token (allow task submission)
     * Returns true if allowed, false if rate limited
     */
    bool try_acquire(const std::string& category = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& bucket = get_bucket(category);
        auto now = std::chrono::steady_clock::now();
        
        // Refill tokens based on elapsed time
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - bucket.last_refill);
        if (elapsed >= bucket.limit.refill_interval) {
            size_t tokens_to_add = (elapsed.count() / bucket.limit.refill_interval.count()) *
                                   bucket.limit.token_refill_amount.count();
            bucket.tokens = std::min(bucket.limit.max_tokens, 
                                     bucket.tokens + tokens_to_add);
            bucket.last_refill = now;
        }
        
        // Try to consume a token
        if (bucket.tokens > 0) {
            --bucket.tokens;
            return true;
        }
        
        return false;
    }

    /**
     * Wait until a token becomes available
     */
    void wait_for_token(const std::string& category = "") {
        while (!try_acquire(category)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

private:
    struct TokenBucket {
        RateLimit limit;
        size_t tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    TokenBucket& get_bucket(const std::string& category) {
        auto it = buckets_.find(category);
        if (it == buckets_.end()) {
            TokenBucket bucket;
            bucket.limit = default_limit_;
            bucket.tokens = default_limit_.max_tokens;
            bucket.last_refill = std::chrono::steady_clock::now();
            buckets_[category] = bucket;
            return buckets_[category];
        }
        return it->second;
    }

    mutable std::mutex mutex_;
    RateLimit default_limit_;
    std::unordered_map<std::string, RateLimit> limits_;
    std::unordered_map<std::string, TokenBucket> buckets_;
};

} // namespace task_engine

