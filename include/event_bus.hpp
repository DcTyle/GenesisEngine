#pragma once
#include <string>
#include <functional>
#include <vector>
#include <mutex>

// Simple event bus for pub/sub pattern
class EventBus {
public:
    using Handler = std::function<void(const std::string& event, const std::string& payload)>;

    void subscribe(const Handler& handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back(handler);
    }

    void publish(const std::string& event, const std::string& payload) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& h : handlers_) {
            h(event, payload);
        }
    }
private:
    std::vector<Handler> handlers_;
    std::mutex mutex_;
};
