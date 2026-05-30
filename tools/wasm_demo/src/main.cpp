// wasm_demo (ADR-0011): proof that real C++ and Rust run inside the sandbox, driving the live
// headless world through the capability-gated Game API.
//
// It boots an authoritative World, loads the player guests that examples/wasm_guests compiled from
// modern C++ (A*) and modern Rust (binary search) to wasm32, runs them on the Wasm3Sandbox backend
// through the production GameApiGateway, and checks each result against host-side ground truth.
//
// Exit code 0 iff every check passed.

#include <cstdint>
#include <cstdio>
#include <queue>
#include <string>
#include <vector>

#include "next/foundation/logger.h"
#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/sandbox.h"
#include "next/sandbox/wasm_sandbox.h"

using namespace Next;
using gameapi::CapabilitySet;
using gameapi::GameApi;
using gameapi::GameApiConfig;

namespace {

int g_pass = 0, g_fail = 0;
void Check(bool ok, const std::string& name, const std::string& observed) {
    std::printf("  [%s] %-40s | %s\n", ok ? "PASS" : "FAIL", name.c_str(), observed.c_str());
    if (ok)
        ++g_pass;
    else
        ++g_fail;
}

std::vector<uint8_t> ReadFile(const std::string& path, bool& ok) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        ok = false;
        return v;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        v.resize(static_cast<size_t>(n));
        ok = std::fread(v.data(), 1, v.size(), f) == v.size();
    } else {
        ok = false;
    }
    std::fclose(f);
    return v;
}

constexpr uint32_t kTagAgent = 1;
constexpr uint32_t kTagObstacle = 2;
constexpr uint32_t kTagBeacon = 3;
constexpr int kW = 8, kH = 8;

sandbox::SandboxPolicy Policy() {
    sandbox::SandboxPolicy p;
    p.memoryBytes = 64 * 1024;
    p.stackSlots = 2048;
    p.maxHostCalls = 100000;
    p.capabilities = CapabilitySet::PlayerDefault();
    return p;
}

// Host-side BFS for ground truth: shortest 4-connected path length on the same grid/obstacles.
int BfsShortest(const std::vector<int>& blocked, int start, int goal) {
    std::vector<int> dist(kW * kH, -1);
    std::queue<int> q;
    dist[start] = 0;
    q.push(start);
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        int c = q.front();
        q.pop();
        if (c == goal)
            return dist[c];
        int cx = c % kW, cy = c / kW;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= kW || ny < 0 || ny >= kH)
                continue;
            int ni = ny * kW + nx;
            if (blocked[ni] || dist[ni] != -1)
                continue;
            dist[ni] = dist[c] + 1;
            q.push(ni);
        }
    }
    return -1;
}

sandbox::RunResult RunWasm(GameApi& api, const std::vector<uint8_t>& wasm, int64_t arg, bool& loaded) {
    api.BeginTick();
    sandbox::GameApiGateway gw(&api);
    auto box = sandbox::MakeWasm3Sandbox();
    std::string err;
    loaded = box->LoadModule(wasm.data(), wasm.size(), &err);
    if (!loaded) {
        std::printf("  [FAIL] module load: %s\n", err.c_str());
        return {};
    }
    return box->Run(Policy(), gw, 0, arg);
}

// ---- A* (C++23 guest) on a world with two staggered walls forcing a real detour ----
void DemoAStar(const std::string& dir) {
    std::printf("\n== C++23 guest: A* pathfinding (sense obstacles -> plan -> MoveTo) ==\n");
    bool fileOk = false;
    auto wasm = ReadFile(dir + "/astar.wasm", fileOk);
    if (!fileOk) {
        Check(false, "astar.wasm present", "missing — C++ toolchain not available at build?");
        return;
    }

    World world;
    gameapi::SimClock clock;
    clock.fixedDt = 0.1;

    Entity agent = world.CreateEntity();
    world.AddComponent<TransformComponent>(agent);  // at (0,0,0)
    gameapi::GameTag atag;
    atag.Set(kTagAgent);
    world.AddComponent<gameapi::GameTag>(agent, atag);

    std::vector<int> blocked(kW * kH, 0);
    auto addObstacle = [&](int x, int y) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = static_cast<float>(x);
        t.position[1] = static_cast<float>(y);
        gameapi::GameTag g;
        g.Set(kTagObstacle);
        world.AddComponent<gameapi::GameTag>(e, g);
        blocked[y * kW + x] = 1;
    };
    for (int y = 0; y <= 5; ++y)
        addObstacle(2, y);  // wall 1: x=2, y=0..5 (gap at y=6,7)
    for (int y = 2; y <= 7; ++y)
        addObstacle(5, y);  // wall 2: x=5, y=2..7 (gap at y=0,1)

    const int start = 0, goal = kW * kH - 1;  // (0,0) -> (7,7)
    const int truth = BfsShortest(blocked, start, goal);

    GameApiConfig cfg;
    cfg.world = &world;
    cfg.clock = &clock;
    cfg.self = gameapi::ToEntityId(agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);

    bool loaded = false;
    sandbox::RunResult r = RunWasm(api, wasm, goal, loaded);
    if (!loaded)
        return;

    Check(r.trap == sandbox::TrapReason::None, "guest ran without trapping", sandbox::ToString(r.trap));
    Check(r.ret == truth, "A* path length matches host BFS",
          "guest=" + std::to_string(r.ret) + " truth=" + std::to_string(truth) + " (manhattan=14, detour forced)");
    Check(r.hostCalls > 0, "guest sensed the world via Game API", "hostCalls=" + std::to_string(r.hostCalls));
    Check(r.fuelUsed > 0, "A* consumed metered CPU fuel (now enforced)", "fuelUsed=" + std::to_string(r.fuelUsed));

    // Actuation: the guest issued a MoveTo toward a valid adjacent first step.
    bool moved = false;
    for (const auto& it : api.Intents().Items()) {
        if (it.type == gameapi::IntentType::MoveTo) {
            int tx = static_cast<int>(it.vec.x + 0.5f), ty = static_cast<int>(it.vec.y + 0.5f);
            int ti = ty * kW + tx;
            bool adjacent = (tx == 0 && ty == 1) || (tx == 1 && ty == 0);  // neighbors of (0,0)
            moved = adjacent && !blocked[ti];
        }
    }
    Check(moved, "guest issued MoveTo to a valid first step", moved ? "ok" : "no/invalid intent");
}

// ---- binary search (Rust 2024 guest) over a sorted row of beacons ----
void DemoBinarySearch(const std::string& dir) {
    std::printf("\n== Rust 2024 guest: binary search over the map (core::slice::binary_search) ==\n");
    bool fileOk = false;
    auto wasm = ReadFile(dir + "/mapsearch.wasm", fileOk);
    if (!fileOk) {
        Check(false, "mapsearch.wasm present", "missing — rustc/wasm32 target not available at build?");
        return;
    }

    World world;
    gameapi::SimClock clock;
    Entity agent = world.CreateEntity();
    world.AddComponent<TransformComponent>(agent);

    const int kBeacons = 12;
    for (int i = 0; i < kBeacons; ++i) {  // beacons at x = 0..11 (ascending id => ascending x)
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = static_cast<float>(i);
        gameapi::GameTag g;
        g.Set(kTagBeacon);
        world.AddComponent<gameapi::GameTag>(e, g);
    }

    GameApiConfig cfg;
    cfg.world = &world;
    cfg.clock = &clock;
    cfg.self = gameapi::ToEntityId(agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);

    struct Case {
        int target, expect;
        const char* note;
    };
    const Case cases[] = {
        {7, 7, "beacon at x=7 -> index 7"}, {0, 0, "first beacon"}, {11, 11, "last beacon"}, {99, -1, "absent -> -1"}};
    for (const Case& c : cases) {
        bool loaded = false;
        sandbox::RunResult r = RunWasm(api, wasm, c.target, loaded);
        if (!loaded)
            return;
        Check(r.trap == sandbox::TrapReason::None && r.ret == c.expect,
              std::string("binary_search(") + std::to_string(c.target) + ")",
              "guest=" + std::to_string(r.ret) + " expect=" + std::to_string(c.expect) + " — " + c.note);
    }
}

// ---- CPU-fuel metering: untrusted code is bounded by load-time gas instrumentation ----
void DemoFuel(const std::string& dir) {
    std::printf("\n== CPU-fuel metering: untrusted code is now BOUNDED (gas instrumentation) ==\n");
    World world;
    gameapi::SimClock clock;
    Entity a = world.CreateEntity();
    world.AddComponent<TransformComponent>(a);
    GameApiConfig cfg;
    cfg.world = &world;
    cfg.clock = &clock;
    cfg.self = gameapi::ToEntityId(a);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);

    auto runFuel = [&](const std::string& file, int64_t arg, uint64_t fuel, sandbox::RunResult& out) -> bool {
        bool ok = false;
        auto wasm = ReadFile(dir + "/" + file, ok);
        if (!ok) {
            Check(false, file + " present", "missing — C++ wasm toolchain unavailable at build?");
            return false;
        }
        api.BeginTick();
        sandbox::GameApiGateway gw(&api);
        auto box = sandbox::MakeWasm3Sandbox();
        std::string err;
        if (!box->LoadModule(wasm.data(), wasm.size(), &err)) {
            Check(false, file + " loads (instrumented)", err);
            return false;
        }
        sandbox::SandboxPolicy p;
        p.memoryBytes = 64 * 1024;
        p.stackSlots = 2048;
        p.maxHostCalls = 100000;
        p.capabilities = CapabilitySet::PlayerDefault();
        p.fuel = fuel;
        out = box->Run(p, gw, 0, arg);
        return true;
    };

    // 1) An infinite loop — would HANG the host without metering — must trap FuelExhausted.
    {
        sandbox::RunResult r;
        if (runFuel("spin.wasm", 0, 2'000'000, r))
            Check(r.trap == sandbox::TrapReason::FuelExhausted && r.fuelUsed > 0, "infinite loop traps FuelExhausted",
                  "trap=" + std::string(sandbox::ToString(r.trap)) + " fuelUsed=" + std::to_string(r.fuelUsed));
    }
    // 2) A counted loop completes under an ample budget, returns the sum, and fuelUsed < budget.
    {
        sandbox::RunResult r;
        const int64_t n = 20000;
        const int64_t expect = 199990000;  // sum 0..19999
        const uint64_t budget = 100'000'000ull;
        if (runFuel("busyloop.wasm", n, budget, r))
            Check(r.trap == sandbox::TrapReason::None && r.ret == expect && r.fuelUsed > 0 && r.fuelUsed < budget,
                  "counted loop completes; fuel scales with work",
                  "ret=" + std::to_string(r.ret) + " fuelUsed=" + std::to_string(r.fuelUsed));
    }
    // 3) The SAME loop under a tight budget traps before finishing — the bound is real.
    {
        sandbox::RunResult r;
        if (runFuel("busyloop.wasm", 20000, 5'000, r))
            Check(r.trap == sandbox::TrapReason::FuelExhausted, "same loop traps under a tight budget",
                  "trap=" + std::string(sandbox::ToString(r.trap)) + " fuelUsed=" + std::to_string(r.fuelUsed));
    }
}

}  // namespace

int main() {
    Logger::Initialize();
    std::printf("=========== Blame Engine — C++/Rust in the WASM sandbox (ADR-0011) ===========\n");
    std::printf("Backend: Wasm3Sandbox | guests compiled from modern C++ (A*) and Rust (binary search)\n");
    const std::string dir = WASM_GUESTS_DIR;
    std::printf("Guest dir: %s\n", dir.c_str());

    DemoAStar(dir);
    DemoBinarySearch(dir);
    DemoFuel(dir);

    std::printf("\n=========== SUMMARY: %d passed, %d failed ===========\n", g_pass, g_fail);
    Logger::Shutdown();
    return g_fail == 0 ? 0 : 1;
}
