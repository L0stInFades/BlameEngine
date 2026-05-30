#pragma once

#include <cstdint>
#include <map>

namespace Next::gameapi {

// Sim-owned, shared store of objective states for the Tasks domain. Reads (GetObjective) hit it
// directly; writes (ReportProgress) go through the intent queue and are applied here by the
// resolver at the tick boundary. std::map keeps iteration deterministic if ever enumerated.
class ObjectiveStore {
public:
    // Returns false if the objective has never been touched.
    bool Get(uint32_t objectiveId, int32_t& outState) const {
        auto it = states_.find(objectiveId);
        if (it == states_.end()) {
            return false;
        }
        outState = it->second;
        return true;
    }

    void Set(uint32_t objectiveId, int32_t state) { states_[objectiveId] = state; }

    void Advance(uint32_t objectiveId, int32_t delta) { states_[objectiveId] += delta; }

    bool Has(uint32_t objectiveId) const { return states_.find(objectiveId) != states_.end(); }
    size_t Size() const { return states_.size(); }

private:
    std::map<uint32_t, int32_t> states_;
};

}  // namespace Next::gameapi
