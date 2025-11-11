#pragma once

#include "task_executor.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <fstream>
#include <sstream>
#include <vector>

namespace task_engine {

/**
 * Simple configuration manager
 * Supports key-value pairs and basic configuration loading
 */
class Config {
public:
    /**
     * Load configuration from a simple key=value file
     */
    static std::unique_ptr<Config> load_from_file(const std::string& filename) {
        auto config = std::make_unique<Config>();
        std::ifstream file(filename);
        std::string line;
        
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                config->set(key, value);
            }
        }
        
        return config;
    }

    /**
     * Set a configuration value
     */
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_[key] = value;
    }

    /**
     * Get a configuration value
     */
    std::string get(const std::string& key, const std::string& default_value = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second : default_value;
    }

    /**
     * Get integer value
     */
    int get_int(const std::string& key, int default_value = 0) const {
        try {
            return std::stoi(get(key, std::to_string(default_value)));
        } catch (...) {
            return default_value;
        }
    }

    /**
     * Get double value
     */
    double get_double(const std::string& key, double default_value = 0.0) const {
        try {
            return std::stod(get(key, std::to_string(default_value)));
        } catch (...) {
            return default_value;
        }
    }

    /**
     * Get boolean value
     */
    bool get_bool(const std::string& key, bool default_value = false) const {
        auto value = get(key);
        if (value == "true" || value == "1" || value == "yes") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no") {
            return false;
        }
        return default_value;
    }

    /**
     * Check if key exists
     */
    bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.find(key) != values_.end();
    }

    /**
     * Get all keys
     */
    std::vector<std::string> keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        result.reserve(values_.size());
        for (const auto& [key, value] : values_) {
            result.push_back(key);
        }
        return result;
    }

    /**
     * Clear all configuration
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> values_;
};

} // namespace task_engine

