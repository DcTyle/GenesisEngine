#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

// Virtual State Dictionary (VSD) manager
class VSDManager {
public:
    void store(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        vsd_[key] = value;
    }
    bool get(const std::string& key, std::string& value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = vsd_.find(key);
        if (it != vsd_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }
    void erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        vsd_.erase(key);
    }
    bool exists(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return vsd_.count(key) > 0;
    }
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> vsd_;
};
