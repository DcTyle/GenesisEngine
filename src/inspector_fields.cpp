#include "inspector_fields.hpp"

size_t EwInspectorFields::upsert(const EwInspectorArtifact& a) {
    for (size_t i = 0; i < artifacts_.size(); ++i) {
        if (artifacts_[i].rel_path == a.rel_path) {
            artifacts_[i] = a;
            revision_u64_++;
            return i;
        }
    }
    artifacts_.push_back(a);
    revision_u64_++;
    return artifacts_.size() - 1;
}

const EwInspectorArtifact* EwInspectorFields::find_by_path(const std::string& rel_path) const {
    for (const auto& a : artifacts_) {
        if (a.rel_path == rel_path) return &a;
    }
    return nullptr;
}

void EwInspectorFields::snapshot_committed(std::vector<EwInspectorArtifact>& out) const {
    out.clear();
    for (const auto& a : artifacts_) {
        if (a.commit_ready) out.push_back(a);
    }
}

void EwInspectorFields::snapshot_all(std::vector<EwInspectorArtifact>& out) const {
    out.clear();
    out.reserve(artifacts_.size());
    for (size_t i = 0; i < artifacts_.size(); ++i) out.push_back(artifacts_[i]);
}

void EwInspectorFields::snapshot_prefix(const std::string& prefix, std::vector<EwInspectorArtifact>& out) const {
    out.clear();
    out.reserve(artifacts_.size());
    for (size_t i = 0; i < artifacts_.size(); ++i) {
        const std::string& p = artifacts_[i].rel_path;
        if (p.size() >= prefix.size() && p.compare(0, prefix.size(), prefix) == 0) out.push_back(artifacts_[i]);
    }
}


void EwInspectorFields::clear_commit_ready() {
    for (auto& a : artifacts_) {
        a.commit_ready = false;
    }
    revision_u64_++;
}
