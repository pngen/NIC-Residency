#pragma once
// Bounded, integrity-checked framed protocol for worker publication and
// coordinator authority.  Frames carry a magic header, a bounded length, a
// message type, and an FNV-1a checksum over the payload.  Malformed /
// truncated / absurd-length / bad-checksum frames are rejected.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>

#include "nicresidency/id.hpp"
#include "nicresidency/records.hpp"

namespace nicresidency {

enum class ProtocolMessageType : std::uint8_t {
  kHello = 1,
  kRegisterWorker = 2,
  kPublishNic = 3,
  kQueryResidency = 4,
  kRetireResidency = 5,
  kReady = 6,
  kAck = 7,
  kError = 8,
};

class FrameCodec {
 public:
  static constexpr std::uint32_t kMagic = 0x4E495243;   // "NIRC"
  static constexpr std::uint32_t kMaxFrame = 65536;
  static constexpr std::size_t kHeaderSize = 13;        // magic(4) len(4) type(1) crc(4)

  // Encode one complete frame.  Throws ResidencyError(kResourceLimit) if the
  // payload exceeds kMaxFrame.
  static std::string encode(ProtocolMessageType type, const std::string& payload);

  // Attempt to decode one frame from a buffer.  Returns true and sets out_*
  // fields + consumed if a complete valid frame is present; returns false if
  // more bytes are needed (incomplete).  Throws ResidencyError(kProtocolError)
  // on malformed header / absurd length / bad checksum / bad enum.
  static bool try_decode(const char* data, std::size_t n,
                         ProtocolMessageType& out_type, std::string& out_payload,
                         std::size_t& consumed);

  static std::uint32_t header_length(const char* data, std::size_t n);
  static bool is_valid_type(std::uint8_t t) noexcept;

  // Payload builders / parsers for the authority messages.
  static std::string payload_register_worker(WorkerId w, WorkerBootId b, CoordinatorEpoch e);
  static std::string payload_publish_nic(WorkerId w, WorkerBootId b, CoordinatorEpoch e,
                                        const NicRecord& nic);
  static bool parse_register_worker(const std::string& p, WorkerId& w, WorkerBootId& b, CoordinatorEpoch& e);
  static bool parse_publish_nic(const std::string& p, WorkerId& w, WorkerBootId& b, CoordinatorEpoch& e, NicRecord& nic);
};

}  // namespace nicresidency
