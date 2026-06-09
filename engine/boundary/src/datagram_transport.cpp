#include "next/boundary/datagram_transport.h"

#include <cstring>

#include "next/boundary/snapshot_codec.h"
#include "next/foundation/endian.h"

namespace Next::boundary {
namespace {

constexpr uint32_t kDatagramPayloadWireMagic = static_cast<uint32_t>('N') | (static_cast<uint32_t>('D') << 8u) |
                                               (static_cast<uint32_t>('G') << 16u) |
                                               (static_cast<uint32_t>('P') << 24u);
constexpr uint16_t kDatagramPayloadWireVersion = 1;
constexpr size_t kDatagramPayloadHeaderBytes = 16;
constexpr size_t kEventWirePayloadBytes = sizeof(uint32_t) + sizeof(uint64_t) + (4 * sizeof(uint32_t));
constexpr size_t kCommandWirePayloadBytes = sizeof(uint32_t) + (4 * sizeof(uint32_t));
constexpr size_t kAckWirePayloadBytes = sizeof(uint64_t);

static_assert(kEventWirePayloadBytes == 28, "Event wire payload size drift");
static_assert(kCommandWirePayloadBytes == 20, "Command wire payload size drift");
static_assert(kAckWirePayloadBytes == 8, "Ack wire payload size drift");

uint32_t FloatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float FloatFromBits(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void AppendF32(std::vector<uint8_t>& out, float value) {
    Next::AppendLE<uint32_t>(out, FloatBits(value));
}

float ReadF32(const uint8_t* data) {
    return FloatFromBits(Next::ReadLE<uint32_t>(data));
}

void BeginSmallMessageFrame(std::vector<uint8_t>& frame, DatagramKind kind, size_t payloadBytes) {
    frame.clear();
    frame.reserve(1 + kDatagramPayloadHeaderBytes + payloadBytes);
    frame.push_back(static_cast<uint8_t>(kind));
    Next::AppendLE<uint32_t>(frame, kDatagramPayloadWireMagic);
    Next::AppendLE<uint16_t>(frame, kDatagramPayloadWireVersion);
    Next::AppendLE<uint16_t>(frame, static_cast<uint16_t>(kDatagramPayloadHeaderBytes));
    frame.push_back(static_cast<uint8_t>(kind));
    frame.push_back(0);
    frame.push_back(0);
    frame.push_back(0);
    Next::AppendLE<uint32_t>(frame, static_cast<uint32_t>(payloadBytes));
}

bool DecodeSmallMessageHeader(const uint8_t* payload, size_t payloadLen, DatagramKind expectedKind,
                              size_t expectedPayloadBytes, const uint8_t*& body) {
    if (payload == nullptr || payloadLen != kDatagramPayloadHeaderBytes + expectedPayloadBytes) {
        return false;
    }
    if (Next::ReadLE<uint32_t>(payload) != kDatagramPayloadWireMagic ||
        Next::ReadLE<uint16_t>(payload + 4) != kDatagramPayloadWireVersion ||
        Next::ReadLE<uint16_t>(payload + 6) != kDatagramPayloadHeaderBytes ||
        payload[8] != static_cast<uint8_t>(expectedKind) || payload[9] != 0 || payload[10] != 0 || payload[11] != 0 ||
        Next::ReadLE<uint32_t>(payload + 12) != expectedPayloadBytes) {
        return false;
    }
    body = payload + kDatagramPayloadHeaderBytes;
    return true;
}

// Prefix a 1-byte kind tag to `payload` in `frame` and hand it to the link.
bool SendFramed(IDatagram* link, std::vector<uint8_t>& frame, DatagramKind kind, const uint8_t* payload,
                size_t payloadLen) {
    frame.resize(1 + payloadLen);
    frame[0] = static_cast<uint8_t>(kind);
    if (payloadLen != 0) {
        std::memcpy(frame.data() + 1, payload, payloadLen);
    }
    return link->Send(frame.data(), frame.size());
}

}  // namespace

void DatagramTransport::PublishSnapshot() {
    if (link_ == nullptr) {
        return;
    }
    std::vector<uint8_t> body;
    EncodeSnapshot(scratch_, body);
    SendFramed(link_, sendBuf_, DatagramKind::Snapshot, body.data(), body.size());
}

bool DatagramTransport::PushEvent(const GameEvent& e) {
    if (link_ == nullptr) {
        return false;
    }
    BeginSmallMessageFrame(sendBuf_, DatagramKind::Event, kEventWirePayloadBytes);
    Next::AppendLE<uint32_t>(sendBuf_, e.type);
    Next::AppendLE<uint64_t>(sendBuf_, e.subject);
    for (float param : e.params) {
        AppendF32(sendBuf_, param);
    }
    return link_->Send(sendBuf_.data(), sendBuf_.size());
}

bool DatagramTransport::PushCommand(const InputCmd& c) {
    if (link_ == nullptr) {
        return false;
    }
    BeginSmallMessageFrame(sendBuf_, DatagramKind::Command, kCommandWirePayloadBytes);
    Next::AppendLE<uint32_t>(sendBuf_, c.type);
    for (float value : c.a) {
        AppendF32(sendBuf_, value);
    }
    return link_->Send(sendBuf_.data(), sendBuf_.size());
}

bool DatagramTransport::PushAck(SnapshotSequence seq) {
    if (link_ == nullptr) {
        return false;
    }
    BeginSmallMessageFrame(sendBuf_, DatagramKind::Ack, kAckWirePayloadBytes);
    Next::AppendLE<uint64_t>(sendBuf_, seq);
    return link_->Send(sendBuf_.data(), sendBuf_.size());
}

void DatagramTransport::Pump() {
    if (link_ == nullptr) {
        return;
    }
    while (link_->Recv(recvBuf_)) {
        if (recvBuf_.empty()) {
            continue;  // malformed empty datagram
        }
        const auto kind = static_cast<DatagramKind>(recvBuf_[0]);
        const uint8_t* payload = recvBuf_.data() + 1;
        const size_t payloadLen = recvBuf_.size() - 1;
        switch (kind) {
            case DatagramKind::Snapshot: {
                SnapshotBlock decoded;
                if (DecodeSnapshot(payload, payloadLen, decoded)) {
                    // Latest-wins by a MONOTONIC high-water mark (never reset on Acquire), so a
                    // reordered/late datagram (seq <= highest ever seen) is dropped, not accepted as
                    // "fresh". Only updates pending_ — never held_ — so a snapshot the consumer is
                    // holding from a prior AcquireSnapshot stays valid across this Pump.
                    if (decoded.sequence > highestSeq_) {
                        highestSeq_ = decoded.sequence;
                        pending_ = std::move(decoded);
                        hasPending_ = true;
                    }
                }
                break;  // bad / older snapshot datagram -> dropped (fail-closed)
            }
            case DatagramKind::Event: {
                const uint8_t* body = nullptr;
                if (DecodeSmallMessageHeader(payload, payloadLen, DatagramKind::Event, kEventWirePayloadBytes, body)) {
                    GameEvent e{};
                    e.type = Next::ReadLE<uint32_t>(body);
                    e.subject = Next::ReadLE<uint64_t>(body + 4);
                    for (int i = 0; i < 4; ++i) {
                        e.params[i] = ReadF32(body + 12 + (i * sizeof(uint32_t)));
                    }
                    events_.push_back(e);
                }
                break;
            }
            case DatagramKind::Command: {
                const uint8_t* body = nullptr;
                if (DecodeSmallMessageHeader(payload, payloadLen, DatagramKind::Command, kCommandWirePayloadBytes,
                                             body)) {
                    InputCmd c{};
                    c.type = Next::ReadLE<uint32_t>(body);
                    for (int i = 0; i < 4; ++i) {
                        c.a[i] = ReadF32(body + 4 + (i * sizeof(uint32_t)));
                    }
                    commands_.push_back(c);
                }
                break;
            }
            case DatagramKind::Ack: {
                const uint8_t* body = nullptr;
                if (DecodeSmallMessageHeader(payload, payloadLen, DatagramKind::Ack, kAckWirePayloadBytes, body)) {
                    acks_.push_back(Next::ReadLE<uint64_t>(body));
                }
                break;
            }
            default:
                break;  // unknown kind -> dropped
        }
    }
}

const SnapshotBlock* DatagramTransport::AcquireSnapshot() {
    Pump();
    if (!hasPending_) {
        return nullptr;  // nothing newer since the last acquire (triple-buffer semantics)
    }
    // Promote pending -> held HERE (the only place held_ changes), so the returned pointer stays valid
    // until the NEXT AcquireSnapshot even if the caller drains events/commands in between.
    held_ = std::move(pending_);
    hasPending_ = false;
    return &held_;
}

bool DatagramTransport::PopEvent(GameEvent& out) {
    Pump();
    if (events_.empty()) {
        return false;
    }
    out = events_.front();
    events_.pop_front();
    return true;
}

bool DatagramTransport::PopCommand(InputCmd& out) {
    Pump();
    if (commands_.empty()) {
        return false;
    }
    out = commands_.front();
    commands_.pop_front();
    return true;
}

bool DatagramTransport::PopAck(SnapshotSequence& outSeq) {
    Pump();
    if (acks_.empty()) {
        return false;
    }
    outSeq = acks_.front();
    acks_.pop_front();
    return true;
}

}  // namespace Next::boundary
