#include "next/boundary/udp_datagram.h"

#if defined(_WIN32)

namespace Next::boundary {

UdpDatagram::~UdpDatagram() = default;

std::unique_ptr<UdpDatagram> UdpDatagram::Open(uint16_t /*localPort*/, std::string* err) {
    if (err != nullptr) {
        *err = "UdpDatagram: no Winsock backend yet; use InProcessTransport / InMemoryDatagramLink";
    }
    return nullptr;
}
bool UdpDatagram::SetPeer(const char* /*host*/, uint16_t /*port*/, std::string* err) {
    if (err != nullptr) {
        *err = "UdpDatagram: unsupported on this platform";
    }
    return false;
}
bool UdpDatagram::Send(const uint8_t* /*data*/, size_t /*len*/) {
    return false;
}
bool UdpDatagram::Recv(std::vector<uint8_t>& /*out*/) {
    return false;
}

}  // namespace Next::boundary

#else  // POSIX

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace Next::boundary {
namespace {
constexpr size_t kMaxDatagram = 65536;  // a UDP datagram never exceeds this; oversized recvs truncate
}

UdpDatagram::~UdpDatagram() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

std::unique_ptr<UdpDatagram> UdpDatagram::Open(uint16_t localPort, std::string* err) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (err != nullptr) {
            *err = std::string("socket: ") + std::strerror(errno);
        }
        return nullptr;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1
    addr.sin_port = htons(localPort);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (err != nullptr) {
            *err = std::string("bind: ") + std::strerror(errno);
        }
        ::close(fd);
        return nullptr;
    }
    // Read back the (possibly ephemeral) bound port.
    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &blen) != 0) {
        if (err != nullptr) {
            *err = std::string("getsockname: ") + std::strerror(errno);
        }
        ::close(fd);
        return nullptr;
    }
    // Non-blocking so Recv() never stalls the caller.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (err != nullptr) {
            *err = std::string("fcntl O_NONBLOCK: ") + std::strerror(errno);
        }
        ::close(fd);
        return nullptr;
    }
    std::unique_ptr<UdpDatagram> d(new UdpDatagram());
    d->fd_ = fd;
    d->localPort_ = ntohs(bound.sin_port);
    return d;
}

bool UdpDatagram::SetPeer(const char* host, uint16_t port, std::string* err) {
    in_addr a{};
    if (host == nullptr || ::inet_pton(AF_INET, host, &a) != 1) {
        if (err != nullptr) {
            *err = "SetPeer: invalid IPv4 host";
        }
        return false;
    }
    peerAddrBe_ = a.s_addr;
    peerPortBe_ = htons(port);
    hasPeer_ = true;
    return true;
}

bool UdpDatagram::Send(const uint8_t* data, size_t len) {
    if (fd_ < 0 || !hasPeer_) {
        return false;
    }
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = peerAddrBe_;
    peer.sin_port = peerPortBe_;
    const ssize_t n = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
    return n == static_cast<ssize_t>(len);
}

bool UdpDatagram::Recv(std::vector<uint8_t>& out) {
    if (fd_ < 0) {
        return false;
    }
    out.resize(kMaxDatagram);
    const ssize_t n = ::recvfrom(fd_, out.data(), out.size(), 0, nullptr, nullptr);
    if (n < 0) {
        out.clear();
        return false;  // EWOULDBLOCK/EAGAIN: nothing available right now
    }
    out.resize(static_cast<size_t>(n));
    return true;
}

}  // namespace Next::boundary

#endif
