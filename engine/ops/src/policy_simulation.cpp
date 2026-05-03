#include "next/ops/policy_simulation.h"

#include "next/ops/ops_workspace.h"
#include "next/serialization/serialization.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Next {
namespace {

std::string ReadTextFile(const std::string& path, std::string* outError) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (outError) {
            *outError = "failed to open " + path;
        }
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string EscapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

int ComputeRouteScore(const OpsRouteInfo& route) {
    int score = route.travelTime;
    score += route.cameraRisk * 3;
    score += route.vehicleCost * 2;
    score -= route.crowdCover;
    return score;
}

int ComputeFinalRisk(const OpsOrderInfo& order, const OpsRouteInfo& route) {
    int risk = order.witnessRisk;
    risk += route.cameraRisk * 5;
    risk += route.vehicleCost * 3;
    risk += std::max(0, route.travelTime - order.deadlineMin) * 4;
    risk -= route.crowdCover * 4;
    risk = std::max(0, std::min(100, risk));
    return risk;
}

const OpsOrderInfo* FindOrder(const OpsCityGraph& graph, const std::string& id) {
    const auto it = std::find_if(graph.orders.begin(), graph.orders.end(), [&](const OpsOrderInfo& order) {
        return order.id == id;
    });
    return it == graph.orders.end() ? nullptr : &(*it);
}

const OpsRouteInfo* FindRoute(const OpsCityGraph& graph, const std::string& id) {
    const auto it = std::find_if(graph.routes.begin(), graph.routes.end(), [&](const OpsRouteInfo& route) {
        return route.id == id;
    });
    return it == graph.routes.end() ? nullptr : &(*it);
}

void SetSimulationError(PolicySimulationResult* result, const std::string& error) {
    if (result) {
        result->accepted = false;
        result->error = error;
        result->outcome = "rejected";
    }
}

} // namespace

bool ParsePolicyResultJson(const std::string& json, PolicyResult* outPolicy, std::string* outError) {
    if (!outPolicy) {
        if (outError) {
            *outError = "outPolicy is null";
        }
        return false;
    }

    auto deserializer = Deserializer::LoadFromString(json, SerializationFormat::JSON);
    if (!deserializer) {
        if (outError) {
            *outError = "failed to parse policy JSON";
        }
        return false;
    }

    if (!deserializer->HasKey("order_id") || !deserializer->HasKey("route_id")) {
        if (outError) {
            *outError = "policy JSON must contain order_id and route_id";
        }
        return false;
    }

    PolicyResult policy;
    policy.orderId = deserializer->ReadString("order_id");
    policy.routeId = deserializer->ReadString("route_id");
    if (policy.orderId.empty() || policy.routeId.empty()) {
        if (outError) {
            *outError = "policy JSON fields must be non-empty strings";
        }
        return false;
    }

    *outPolicy = policy;
    return true;
}

bool LoadOpsCityGraph(const OpsWorkspace& workspace, OpsCityGraph* outGraph, std::string* outError) {
    if (!outGraph) {
        if (outError) {
            *outError = "outGraph is null";
        }
        return false;
    }

    const std::filesystem::path graphPath = std::filesystem::path(workspace.RootPath()) / "city_graph.json";
    const std::string json = ReadTextFile(graphPath.string(), outError);
    if (json.empty()) {
        if (outError && outError->empty()) {
            *outError = "city_graph.json is empty";
        }
        return false;
    }

    auto deserializer = Deserializer::LoadFromString(json, SerializationFormat::JSON);
    if (!deserializer) {
        if (outError) {
            *outError = "failed to parse city_graph.json";
        }
        return false;
    }

    OpsCityGraph graph;
    if (!deserializer->BeginArray("orders")) {
        if (outError) {
            *outError = "city_graph.json must contain orders array";
        }
        return false;
    }
    const size_t orderCount = deserializer->GetArraySize();
    for (size_t i = 0; i < orderCount; ++i) {
        if (!deserializer->BeginObject("")) {
            if (outError) {
                *outError = "invalid order object";
            }
            return false;
        }
        OpsOrderInfo order;
        order.id = deserializer->ReadString("id");
        order.witnessRisk = deserializer->ReadInt32("witness_risk");
        order.deadlineMin = deserializer->ReadInt32("deadline_min");
        deserializer->EndObject();
        if (!order.id.empty()) {
            graph.orders.push_back(order);
        }
    }
    deserializer->EndArray();

    if (!deserializer->BeginArray("routes")) {
        if (outError) {
            *outError = "city_graph.json must contain routes array";
        }
        return false;
    }
    const size_t routeCount = deserializer->GetArraySize();
    for (size_t i = 0; i < routeCount; ++i) {
        if (!deserializer->BeginObject("")) {
            if (outError) {
                *outError = "invalid route object";
            }
            return false;
        }
        OpsRouteInfo route;
        route.id = deserializer->ReadString("id");
        route.travelTime = deserializer->ReadInt32("travel_time");
        route.cameraRisk = deserializer->ReadInt32("camera_risk");
        route.vehicleCost = deserializer->ReadInt32("vehicle_cost");
        route.crowdCover = deserializer->ReadInt32("crowd_cover");
        deserializer->EndObject();
        if (!route.id.empty()) {
            graph.routes.push_back(route);
        }
    }
    deserializer->EndArray();

    if (graph.orders.empty() || graph.routes.empty()) {
        if (outError) {
            *outError = "city graph must contain at least one order and route";
        }
        return false;
    }

    *outGraph = graph;
    return true;
}

bool SimulatePolicyResult(const OpsWorkspace& workspace,
                          const PolicyResult& policy,
                          PolicySimulationResult* outResult) {
    if (!outResult) {
        return false;
    }

    PolicySimulationResult result;
    result.orderId = policy.orderId;
    result.routeId = policy.routeId;

    OpsCityGraph graph;
    std::string error;
    if (!LoadOpsCityGraph(workspace, &graph, &error)) {
        SetSimulationError(&result, error);
        *outResult = result;
        return false;
    }

    const OpsOrderInfo* order = FindOrder(graph, policy.orderId);
    if (!order) {
        SetSimulationError(&result, "unknown order_id: " + policy.orderId);
        *outResult = result;
        return false;
    }

    const OpsRouteInfo* route = FindRoute(graph, policy.routeId);
    if (!route) {
        SetSimulationError(&result, "unknown route_id: " + policy.routeId);
        *outResult = result;
        return false;
    }

    result.accepted = true;
    result.routeScore = ComputeRouteScore(*route);
    result.initialWitnessRisk = order->witnessRisk;
    result.finalWitnessRisk = ComputeFinalRisk(*order, *route);
    result.riskDelta = result.finalWitnessRisk - result.initialWitnessRisk;
    result.deadlineMet = route->travelTime <= order->deadlineMin;
    result.outcome = result.finalWitnessRisk <= order->witnessRisk ? "risk_reduced" : "risk_increased";
    *outResult = result;
    return true;
}

bool SimulatePolicyJson(const OpsWorkspace& workspace,
                        const std::string& policyJson,
                        PolicySimulationResult* outResult) {
    PolicyResult policy;
    std::string error;
    if (!ParsePolicyResultJson(policyJson, &policy, &error)) {
        SetSimulationError(outResult, error);
        return false;
    }
    return SimulatePolicyResult(workspace, policy, outResult);
}

std::string PolicySimulationResultToJson(const PolicySimulationResult& result) {
    std::ostringstream out;
    out << "{";
    out << "\"accepted\":" << (result.accepted ? "true" : "false");
    out << ",\"order_id\":\"" << EscapeJsonString(result.orderId) << "\"";
    out << ",\"route_id\":\"" << EscapeJsonString(result.routeId) << "\"";
    out << ",\"route_score\":" << result.routeScore;
    out << ",\"initial_witness_risk\":" << result.initialWitnessRisk;
    out << ",\"final_witness_risk\":" << result.finalWitnessRisk;
    out << ",\"risk_delta\":" << result.riskDelta;
    out << ",\"deadline_met\":" << (result.deadlineMet ? "true" : "false");
    out << ",\"outcome\":\"" << EscapeJsonString(result.outcome) << "\"";
    if (!result.error.empty()) {
        out << ",\"error\":\"" << EscapeJsonString(result.error) << "\"";
    }
    out << "}";
    return out.str();
}

} // namespace Next
