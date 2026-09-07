// FrameCodec implementation: bounded, integrity-checked framed protocol.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/authority.hpp"

#include <cstring>

#include "nicresidency/error.hpp"

namespace nicresidency {

namespace {

std::uint64_t fnv1a(const std::string& data) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (unsigned char c : data) { h ^= c; h *= 0x100000001b3ULL; }
  return h;
}

void put_u32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8*i)) & 0xFF)); }
void put_u64(std::string& s, std::uint64_t v) { for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8*i)) & 0xFF)); }
void put_u8(std::string& s, std::uint8_t v) { s.push_back(static_cast<char>(v)); }
void put_str(std::string& s, const std::string& v) {
  if (v.size() > 0xFFFF) throw ResidencyError(ErrorCode::kProtocolError, "string field too large for frame");
  put_u32(s, static_cast<std::uint32_t>(v.size())); s.append(v);
}

class R {
 public:
  explicit R(const std::string& d) : d_(d) {}
  std::uint8_t u8() { need(1); return static_cast<std::uint8_t>(d_[p_++]); }
  std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i=0;i<4;++i) v |= (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d_[p_+i])) << (8*i)); p_ += 4; return v; }
  std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i=0;i<8;++i) v |= (static_cast<std::uint64_t>(static_cast<std::uint8_t>(d_[p_+i])) << (8*i)); p_ += 8; return v; }
  std::string str() { std::uint32_t n = u32(); need(n); std::string s = d_.substr(p_, n); p_ += n; return s; }
  std::size_t remaining() const noexcept { return d_.size() - p_; }
 private:
  void need(std::size_t n) { if (n > d_.size() - p_) throw ResidencyError(ErrorCode::kProtocolError, "protocol payload truncated"); }
  const std::string& d_;
  std::size_t p_{0};
};

std::uint32_t to_u32_le(const char* p) {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

}  // namespace

std::uint32_t FrameCodec::header_length(const char* data, std::size_t n) {
  if (n < 8) return 0;
  std::uint32_t magic = to_u32_le(data);
  if (magic != kMagic) throw ResidencyError(ErrorCode::kProtocolError, "bad frame magic");
  std::uint32_t len = to_u32_le(data + 4);
  if (len < kHeaderSize || len > kMaxFrame) throw ResidencyError(ErrorCode::kProtocolError, "frame length out of bounds");
  return len;
}

bool FrameCodec::is_valid_type(std::uint8_t t) noexcept {
  return t >= 1 && t <= 8;
}

std::string FrameCodec::encode(ProtocolMessageType type, const std::string& payload) {
  if (payload.size() + kHeaderSize > kMaxFrame) {
    throw ResidencyError(ErrorCode::kResourceLimit, "frame too large");
  }
  std::string out;
  put_u32(out, kMagic);
  put_u32(out, static_cast<std::uint32_t>(kHeaderSize + payload.size()));
  put_u8(out, static_cast<std::uint8_t>(type));
  std::uint64_t sum = fnv1a(payload);
  put_u32(out, static_cast<std::uint32_t>(sum & 0xFFFFFFFFu));
  out.append(payload);
  return out;
}

bool FrameCodec::try_decode(const char* data, std::size_t n, ProtocolMessageType& out_type,
                            std::string& out_payload, std::size_t& consumed) {
  if (n < kHeaderSize) return false;  // incomplete header
  std::uint32_t len = header_length(data, n);  // throws on bad magic/length
  if (len > n) return false;  // incomplete frame body
  std::uint8_t type = static_cast<std::uint8_t>(data[8]);
  if (!is_valid_type(type)) throw ResidencyError(ErrorCode::kProtocolError, "invalid protocol message type");
  std::uint64_t sum = fnv1a(std::string(data + kHeaderSize, len - kHeaderSize));
  std::uint32_t stored = to_u32_le(data + 9);
  if (static_cast<std::uint32_t>(sum & 0xFFFFFFFFu) != stored) {
    throw ResidencyError(ErrorCode::kProtocolError, "frame checksum mismatch");
  }
  out_type = static_cast<ProtocolMessageType>(type);
  out_payload.assign(data + kHeaderSize, len - kHeaderSize);
  consumed = len;
  return true;
}

std::string FrameCodec::payload_register_worker(WorkerId w, WorkerBootId b, CoordinatorEpoch e) {
  std::string s; put_u64(s, w.value()); put_u64(s, b.value()); put_u64(s, e.value()); return s;
}

bool FrameCodec::parse_register_worker(const std::string& p, WorkerId& w, WorkerBootId& b, CoordinatorEpoch& e) {
  try { R r(p); w = WorkerId(r.u64()); b = WorkerBootId(r.u64()); e = CoordinatorEpoch(r.u64()); return r.remaining() == 0; }
  catch (const ResidencyError&) { return false; }
}

std::string FrameCodec::payload_publish_nic(WorkerId w, WorkerBootId b, CoordinatorEpoch e, const NicRecord& nic) {
  std::string s;
  put_u64(s, w.value()); put_u64(s, b.value()); put_u64(s, e.value());
  put_u64(s, nic.id.value()); put_u64(s, nic.generation.value());
  put_u32(s, static_cast<std::uint32_t>(nic.pci.numeric()));
  put_u64(s, nic.numa_node.value());
  put_str(s, nic.os_name);
  return s;
}

bool FrameCodec::parse_publish_nic(const std::string& p, WorkerId& w, WorkerBootId& b, CoordinatorEpoch& e, NicRecord& nic) {
  try {
    R r(p);
    w = WorkerId(r.u64()); b = WorkerBootId(r.u64()); e = CoordinatorEpoch(r.u64());
    nic.id = NicId(r.u64()); nic.generation = NicGeneration(r.u64());
    std::uint32_t bdf = r.u32();
    nic.pci = PciAddress::from_bdf(static_cast<std::uint16_t>(bdf >> 16), static_cast<std::uint16_t>(bdf & 0xFFFF));
    nic.numa_node = NumaNodeId(r.u64());
    nic.os_name = r.str();
    nic.provenance = Provenance(EvidenceKind::kReal, {Source::kRdmaProvider});
    nic.lifecycle = LifecycleState::kAvailable;
    nic.freshness = 1;
    return r.remaining() == 0;
  } catch (const ResidencyError&) { return false; }
}

}  // namespace nicresidency
