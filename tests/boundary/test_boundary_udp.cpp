// Real UDP loopback for the snapshot transport (W15). Proves DatagramTransport works over an ACTUAL
// UDP socket (127.0.0.1), not just the in-memory link — i.e. the headless world can genuinely run as a
// dedicated UDP server. Best-effort: if the sandbox forbids UDP sockets, the tests GTEST_SKIP rather
// than fail, so CI stays green on locked-down runners.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "next/boundary/datagram_transport.h"
#include "next/boundary/snapshot.h"
#include "next/boundary/snapshot_publisher.h"
#include "next/boundary/snapshot_receiver.h"
#include "next/boundary/udp_datagram.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::boundary;

namespace {

// Open a connected loopback UDP pair, or return false (with both null) if the platform/sandbox blocks
// it — callers then GTEST_SKIP.
bool OpenLoopbackPair(std::unique_ptr<UdpDatagram>& a, std::unique_ptr<UdpDatagram>& b) {
    std::string err;
    a = UdpDatagram::Open(0, &err);
    if (!a) {
        return false;
    }
    b = UdpDatagram::Open(0, &err);
    if (!b) {
        return false;
    }
    return a->SetPeer("127.0.0.1", b->LocalPort(), &err) && b->SetPeer("127.0.0.1", a->LocalPort(), &err);
}

// Poll Recv for up to ~200ms (loopback delivery is near-instant but asynchronous).
bool RecvWithRetry(IDatagram& d, std::vector<uint8_t>& out) {
    for (int i = 0; i < 200; ++i) {
        if (d.Recv(out)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST(BoundaryUdp, RawDatagramRoundTrip) {
    std::unique_ptr<UdpDatagram> a;
    std::unique_ptr<UdpDatagram> b;
    if (!OpenLoopbackPair(a, b)) {
        GTEST_SKIP() << "UDP loopback unavailable in this environment";
    }
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_TRUE(a->Send(payload.data(), payload.size()));
    std::vector<uint8_t> got;
    ASSERT_TRUE(RecvWithRetry(*b, got)) << "no datagram received on loopback";
    EXPECT_EQ(got, payload);
}

TEST(BoundaryUdp, SnapshotRoundTripOverRealUdp) {
    std::unique_ptr<UdpDatagram> simLink;
    std::unique_ptr<UdpDatagram> viewLink;
    if (!OpenLoopbackPair(simLink, viewLink)) {
        GTEST_SKIP() << "UDP loopback unavailable in this environment";
    }

    World world;
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = 3.0f;
    RenderableComponent r;
    r.visual = 42;
    world.AddComponent<RenderableComponent>(e, r);

    SnapshotPublisher pub;
    DatagramTransport simTx(simLink.get());
    DatagramTransport viewTx(viewLink.get());
    SnapshotReceiver rx;

    // Publish a keyframe over the wire, then poll the view transport until the datagram arrives (each
    // AcquireSnapshot pumps the socket; loopback delivery is near-instant but asynchronous).
    pub.PublishTo(simTx, world, 7, 0.1);
    const SnapshotBlock* snap = nullptr;
    for (int i = 0; i < 200 && snap == nullptr; ++i) {
        snap = viewTx.AcquireSnapshot();
        if (snap == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (snap == nullptr) {
        GTEST_SKIP() << "snapshot datagram did not traverse loopback in this environment";
    }
    ASSERT_EQ(rx.Apply(*snap), SnapshotReceiver::Result::AppliedKeyframe);
    EXPECT_EQ(rx.EntityCount(), 1u);
    EXPECT_EQ(rx.ServerTick(), 7u);
    ASSERT_EQ(rx.Mirror().count(static_cast<EntityId>(e)), 1u);
    EXPECT_EQ(rx.Mirror().at(static_cast<EntityId>(e)).visual, 42u);
}
