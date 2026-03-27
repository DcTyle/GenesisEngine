#pragma once
#include <thread>
#include <atomic>
#include <functional>

// BIOS Scheduler for orchestrating tasks
class BiosScheduler {
public:
    using Task = std::function<void()>;
    BiosScheduler() : running_(false) {}
    void start(const Task& main_task) {
        running_ = true;
        thread_ = std::thread([this, main_task]() {
            while (running_) {
                main_task();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    ~BiosScheduler() { stop(); }
private:
    std::thread thread_;
    std::atomic<bool> running_;
};
