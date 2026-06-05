#pragma once

#include <cstdint>

// Client render clock (ADR-0006 W14). The client (UE5) renders at its own frame rate but is NOT
// authoritative: it must FOLLOW the server's time carried by snapshots, never simulate ahead. This
// clock advances by local frame dt for smoothness, yet is hard-bounded by the latest authoritative
// server time:
//   * it NEVER runs past the server (no extrapolation past authority — the anti-cheat red line);
//   * it interpolates slightly BEHIND the server (interpolationDelay) so there are always two
//     snapshots to blend between, avoiding hitches when one is dropped (which the B1 protocol allows);
//   * if it falls too far behind (a long stall, then a fresh snapshot), it SNAPS forward rather than
//     fast-forwarding through stale time.
// It holds no game state — purely a render-time follower — so it is deterministic given its inputs.
// PRECONDITION: serverSeconds is the authoritative time and is non-decreasing across Tick() calls
// (SnapshotReceiver::ServerSeconds() guarantees this — it is monotonic). The clamps assume that; feeding
// a decreasing serverSeconds would pull the render time backward (garbage in, garbage out).

namespace Next::boundary {

class RenderClock {
public:
    // interpolationDelay: how far behind the latest server time to render (seconds). maxBehind: the
    // largest gap tolerated before snapping forward (seconds). catchup: fraction of the remaining gap
    // closed per frame when behind, so a steady stream CONVERGES to exactly `delay` behind authority
    // instead of holding a constant lag. Defaults are a couple of sim ticks.
    explicit RenderClock(double interpolationDelay = 1.0 / 30.0, double maxBehind = 0.5, double catchup = 0.1)
        : interpolationDelay_(interpolationDelay), maxBehind_(maxBehind), catchup_(catchup) {}

    // Advance by one render frame of `localDt` toward the authoritative `serverSeconds`. Returns the
    // render time to sample the mirror at, always in [serverSeconds - delay - maxBehind, serverSeconds
    // - delay] — i.e. behind the delayed authority, NEVER ahead of it. Monotonic non-decreasing.
    double Tick(double localDt, double serverSeconds) {
        if (localDt < 0.0) {
            localDt = 0.0;
        }
        const double target = serverSeconds - interpolationDelay_;  // render this far behind authority
        renderSeconds_ += localDt;
        if (renderSeconds_ < target) {
            // Close a fraction of the remaining gap so we converge to the target (not a fixed lag).
            renderSeconds_ += (target - renderSeconds_) * catchup_;
        }
        if (renderSeconds_ > target) {
            renderSeconds_ = target;  // never run past the (delayed) authority
        }
        if (renderSeconds_ < target - maxBehind_) {
            renderSeconds_ = target - maxBehind_;  // stall recovery: snap forward, don't replay stale time
        }
        if (renderSeconds_ < 0.0) {
            renderSeconds_ = 0.0;  // before the first snapshot
        }
        return renderSeconds_;
    }

    double RenderSeconds() const { return renderSeconds_; }
    void Reset() { renderSeconds_ = 0.0; }

private:
    double interpolationDelay_;
    double maxBehind_;
    double catchup_;
    double renderSeconds_ = 0.0;
};

}  // namespace Next::boundary
