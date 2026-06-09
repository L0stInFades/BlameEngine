#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "next/boundary/datagram_transport.h"  // IDatagram

// A real UDP socket exposed as an IDatagram (ADR-0006 W15). Plug it into DatagramTransport and the
// headless authoritative world becomes a dedicated UDP server; UE5 (or another process) is the peer.
// POSIX sockets, non-blocking; the socket header dependency stays in the .cpp (this header is clean).
// On a platform without the POSIX backend (e.g. Windows, until a Winsock impl lands) Open() returns
// nullptr and sets *err — callers fall back to InProcessTransport / InMemoryDatagramLink.

namespace Next::boundary {

class UdpDatagram final : public IDatagram {
public:
    ~UdpDatagram() override;

    // Bind a non-blocking UDP socket to 127.0.0.1:localPort (0 = an ephemeral port; read it back with
    // LocalPort()). Returns nullptr and sets *err on failure / unsupported platform.
    static std::unique_ptr<UdpDatagram> Open(uint16_t localPort, std::string* err);

    // Where Send() goes (the peer's host:port). Set once both endpoints know each other's ports.
    bool SetPeer(const char* host, uint16_t port, std::string* err);

    uint16_t LocalPort() const { return localPort_; }

    bool Send(const uint8_t* data, size_t len) override;  // sendto the peer; best-effort (UDP)
    bool Recv(std::vector<uint8_t>& out) override;        // non-blocking recvfrom; false if nothing now

private:
    UdpDatagram() = default;

    int fd_ = -1;
    uint16_t localPort_ = 0;
    // Peer address kept as raw fields so the header needs no <netinet/in.h>; resolved in the .cpp.
    uint32_t peerAddrBe_ = 0;  // network-byte-order IPv4
    uint16_t peerPortBe_ = 0;  // network-byte-order port
    bool hasPeer_ = false;
};

}  // namespace Next::boundary
