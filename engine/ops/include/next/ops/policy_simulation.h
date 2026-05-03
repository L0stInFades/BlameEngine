#pragma once

#include <string>
#include <vector>

namespace Next {

class OpsWorkspace;

struct PolicyResult {
    std::string orderId;
    std::string routeId;
};

struct OpsRouteInfo {
    std::string id;
    int travelTime = 0;
    int cameraRisk = 0;
    int vehicleCost = 0;
    int crowdCover = 0;
};

struct OpsOrderInfo {
    std::string id;
    int witnessRisk = 0;
    int deadlineMin = 0;
};

struct OpsCityGraph {
    std::vector<OpsOrderInfo> orders;
    std::vector<OpsRouteInfo> routes;
};

struct PolicySimulationResult {
    bool accepted = false;
    std::string error;
    std::string orderId;
    std::string routeId;
    int routeScore = 0;
    int initialWitnessRisk = 0;
    int finalWitnessRisk = 0;
    int riskDelta = 0;
    bool deadlineMet = false;
    std::string outcome;
};

bool ParsePolicyResultJson(const std::string& json, PolicyResult* outPolicy, std::string* outError = nullptr);
bool LoadOpsCityGraph(const OpsWorkspace& workspace, OpsCityGraph* outGraph, std::string* outError = nullptr);
bool SimulatePolicyResult(const OpsWorkspace& workspace,
                          const PolicyResult& policy,
                          PolicySimulationResult* outResult);
bool SimulatePolicyJson(const OpsWorkspace& workspace,
                        const std::string& policyJson,
                        PolicySimulationResult* outResult);

std::string PolicySimulationResultToJson(const PolicySimulationResult& result);

} // namespace Next
