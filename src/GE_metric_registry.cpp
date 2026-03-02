#include "GE_metric_registry.hpp"

namespace genesis {

MetricRegistry::MetricRegistry() {
    completed_.reserve(64);
}

uint64_t MetricRegistry::enqueue_task(const MetricTask& t_in) {
    MetricTask t = t_in;
    if (t.task_id_u64 == 0) {
        t.task_id_u64 = seq_u64_++;
    }
    q_.push_back(t);
    return t.task_id_u64;
}

MetricTask* MetricRegistry::front() {
    if (q_.empty()) return nullptr;
    return &q_.front();
}

void MetricRegistry::pop_front() {
    if (!q_.empty()) q_.pop_front();
}

void MetricRegistry::record_completed(const MetricTask& t) {
    // Bounded record list (ring-like behavior by drop-oldest).
    if (completed_.size() >= 128) {
        completed_.erase(completed_.begin());
    }
    completed_.push_back(t);
}

uint64_t MetricRegistry::accepted_since_tick_u64(uint64_t tick_min_u64) const {
    uint64_t c = 0;
    for (const auto& t : completed_) {
        if (!t.accepted) continue;
        if (t.completed_tick_u64 >= tick_min_u64) c += 1u;
    }
    return c;
}

} // namespace genesis
