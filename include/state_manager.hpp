#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

// State manager for runtime and persistent state
class StateManager {
public:
    void store(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_[key] = value;
    }
    bool get(const std::string& key, std::string& value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = state_.find(key);
        if (it != state_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }
    void snapshot(const std::string& filename) const;
    void restore(const std::string& filename);
    void mark_timestamp(const std::string& label);
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> state_;
};
