#pragma once
#include <string>
#include <atomic>

// BIOS Loader for runtime loading and shutdown
class BiosLoader {
public:
    BiosLoader() : loaded_(false) {}
    void load() { loaded_ = true; }
    void shutdown() { loaded_ = false; }
    bool status() const { return loaded_; }
    void publish_event(const std::string& event) {/* TODO: integrate with EventBus */}
private:
    std::atomic<bool> loaded_;
};
