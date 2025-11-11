#pragma once

#include "task.hpp"
#include <unordered_map>
#include <mutex>
#include <optional>
#include <chrono>

namespace task_engine {

/**
 * Simple in-memory cache for task results
 */
template<typename Key = std::string>
class TaskResultCache {
public:
    struct CacheEntry {
        TaskResult result;
        std::chrono::steady_clock::time_point expiry_time;
    };

    explicit TaskResultCache(std::chrono::milliseconds default_ttl = std::chrono::minutes(5))
        : default_ttl_(default_ttl)
    {}

    /**
     * Store a result in the cache
     */
    void put(const Key& key, const TaskResult& result,
             std::optional<std::chrono::milliseconds> ttl = std::nullopt) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto expiry = std::chrono::steady_clock::now() + (ttl.value_or(default_ttl_));
        cache_[key] = CacheEntry{result, expiry};
    }

    /**
     * Get a result from the cache
     */
    std::optional<TaskResult> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }

        auto now = std::chrono::steady_clock::now();
        if (now > it->second.expiry_time) {
            cache_.erase(it);
            return std::nullopt;
        }

        return it->second.result;
    }

    /**
     * Check if a key exists and is valid
     */
    bool contains(const Key& key) {
        return get(key).has_value();
    }

    /**
     * Remove a key from the cache
     */
    void remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(key);
    }

    /**
     * Clear all expired entries
     */
    void cleanup_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now > it->second.expiry_time) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * Clear all entries
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }

    /**
     * Get cache size
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<Key, CacheEntry> cache_;
    std::chrono::milliseconds default_ttl_;
};

} // namespace task_engine

