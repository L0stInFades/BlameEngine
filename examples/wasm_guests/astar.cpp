// Classic A* pathfinding — player code written in modern C++ (C++23), compiled to wasm32 and run
// inside the sandbox against the live headless world (ADR-0011).
//
// The guest knows NOTHING a priori: it discovers the map purely through the capability-gated Game
// API. It senses every OBSTACLE entity, reads each one's grid cell, builds an occupancy grid in
// its OWN linear memory, runs A* (4-connected, Manhattan heuristic) from its own cell to the goal,
// then issues a MoveTo intent toward the first step of the optimal path. It returns the path length
// in steps (or -1 if unreachable) so the host can cross-check it against ground truth.
//
// Freestanding: no libc, no STL, no heap — A*'s open/closed sets are fixed-size arrays over an
// 8x8 grid. This is exactly the "player writes real C++" story the moat promised.

#include "guest_api.h"

namespace {
constexpr int kW = 8;
constexpr int kH = 8;
constexpr int kN = kW * kH;
constexpr int kInf = 1 << 30;
constexpr uint32_t kTagObstacle = 2;

bool g_blocked[kN];
int g_g[kN];      // cost from start
int g_f[kN];      // g + heuristic
int g_from[kN];   // predecessor on the best path
bool g_open[kN];  // in the open set
bool g_closed[kN];

int Heuristic(int a, int b) {
    int ax = a % kW, ay = a / kW, bx = b % kW, by = b / kW;
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx + dy;
}

int ToCell(float v) {
    return static_cast<int>(v + 0.5f);
}
}  // namespace

extern "C" int run(int arg) {
    for (int i = 0; i < kN; ++i) {
        g_blocked[i] = false;
        g_g[i] = kInf;
        g_f[i] = kInf;
        g_from[i] = -1;
        g_open[i] = false;
        g_closed[i] = false;
    }

    // 1. Sense the map: every OBSTACLE entity, then its position -> a blocked cell.
    uint64_t ids[128];
    int count = api_query_by_tag(kTagObstacle, ids, 128);
    if (count > 128)
        count = 128;
    for (int i = 0; i < count; ++i) {
        Vec3 p;
        if (api_get_position(ids[i], p)) {
            int x = ToCell(p.x), y = ToCell(p.y);
            if (x >= 0 && x < kW && y >= 0 && y < kH)
                g_blocked[y * kW + x] = true;
        }
    }

    // 2. Start = own cell (sensed via Self+GetPosition); goal = arg cell (default bottom-right).
    int start = 0;
    Vec3 sp;
    if (api_get_position(api_self(), sp)) {
        int x = ToCell(sp.x), y = ToCell(sp.y);
        if (x >= 0 && x < kW && y >= 0 && y < kH)
            start = y * kW + x;
    }
    int goal = (arg >= 0 && arg < kN) ? arg : (kN - 1);
    if (g_blocked[start] || g_blocked[goal])
        return -1;

    // 3. A* with a linear-scan open set (kN is tiny; classic and deterministic).
    g_g[start] = 0;
    g_f[start] = Heuristic(start, goal);
    g_open[start] = true;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (true) {
        int cur = -1, best = kInf;
        for (int i = 0; i < kN; ++i) {
            if (g_open[i] && g_f[i] < best) {
                best = g_f[i];
                cur = i;
            }
        }
        if (cur < 0)
            return -1;  // open set empty -> no path
        if (cur == goal)
            break;
        g_open[cur] = false;
        g_closed[cur] = true;
        int cx = cur % kW, cy = cur / kW;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= kW || ny < 0 || ny >= kH)
                continue;
            int ni = ny * kW + nx;
            if (g_blocked[ni] || g_closed[ni])
                continue;
            int tentative = g_g[cur] + 1;
            if (tentative < g_g[ni]) {
                g_from[ni] = cur;
                g_g[ni] = tentative;
                g_f[ni] = tentative + Heuristic(ni, goal);
                g_open[ni] = true;
            }
        }
    }

    if (g_g[goal] >= kInf)
        return -1;

    // 4. Act: MoveTo the first step on the optimal path (sense -> compute -> actuate, end to end).
    if (g_g[goal] > 0) {
        int step = goal;
        while (g_from[step] != -1 && g_from[step] != start)
            step = g_from[step];
        api_move_to(static_cast<float>(step % kW), static_cast<float>(step / kW), 0.0f, 1.0f);
    }
    return g_g[goal];  // shortest-path length in steps
}
