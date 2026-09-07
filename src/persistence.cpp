// PersistenceStore implementation with checked serialization + integrity.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/persistence.hpp"
#include "nicresidency/version.hpp"

#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "nicresidency/error.hpp"

namespace nicresidency {

namespace {

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;

constexpr char kMagicBox[] = "NICRESIDENCYPERSIST";
constexpr std::size_t kMagicLen = sizeof(kMagicBox) - 1;

class Writer {
 public:
  std::string buf;
  void u8(uint8_t v) { buf.push_back(static_cast<char>(v)); }
  void u16(uint16_t v) { u8(static_cast<uint8_t>(v & 0xFF)); u8(static_cast<uint8_t>((v >> 8) & 0xFF)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF)); }
  void u64(uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF)); }
  void str(const std::string& s) {
    if (s.size() > 0xFFFF) throw ResidencyError(ErrorCode::kResourceLimit, "string too large to persist");
    u16(static_cast<uint16_t>(s.size()));
    buf.append(s);
  }
};

class Reader {
 public:
  explicit Reader(const std::string& data) : data_(data) {}
  std::uint8_t u8() { need(1); return static_cast<std::uint8_t>(data_[pos_++]); }
  std::uint16_t u16() { need(2); std::uint16_t v = static_cast<std::uint16_t>(data_[pos_]) | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data_[pos_+1])) << 8); pos_ += 2; return v; }
  std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_+i])) << (8*i)); pos_ += 4; return v; }
  std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[pos_+i])) << (8*i)); pos_ += 8; return v; }
  std::string str() {
    std::uint16_t n = u16();
    need(n);
    std::string s = data_.substr(pos_, n);
    pos_ += n;
    return s;
  }
  bool at_end() const noexcept { return pos_ == data_.size(); }
  std::size_t remaining() const noexcept { return data_.size() - pos_; }
 private:
  void need(std::size_t n) {
    if (n > data_.size() - pos_) {
      throw ResidencyError(ErrorCode::kIntegrityFailure, "truncated persistence stream");
    }
  }
  const std::string& data_;
  std::size_t pos_{0};
};

uint64_t fnv1a(const std::string& data) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (unsigned char c : data) { h ^= c; h *= 0x100000001b3ULL; }
  return h;
}

void write_provenance(Writer& w, const Provenance& p) {
  w.u8(static_cast<uint8_t>(p.kind()));
  w.u32(static_cast<uint32_t>(p.sources().size()));
  for (Source s : p.sources()) w.u8(static_cast<uint8_t>(s));
}

Provenance read_provenance(Reader& r) {
  uint8_t k = r.u8();
  if (k > static_cast<uint8_t>(EvidenceKind::kUnsupported)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid evidence kind");
  uint32_t n = r.u32();
  if (n > 64) throw ResidencyError(ErrorCode::kIntegrityFailure, "provenance source count too large");
  std::vector<Source> srcs;
  for (uint32_t i = 0; i < n; ++i) {
    uint8_t s = r.u8();
    if (s > static_cast<uint8_t>(Source::kSyntheticFixture)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid provenance source");
    srcs.push_back(static_cast<Source>(s));
  }
  return Provenance(static_cast<EvidenceKind>(k), srcs);
}

void write_capability(Writer& w, const CapabilitySet& c) {
  w.u64(c.generation().value());
  uint32_t flags = 0;
  if (c.rdma_capable()) flags |= 1u;
  if (c.sr_iov_capable()) flags |= 2u;
  if (c.offload_capable()) flags |= 4u;
  if (c.local_memory_observable()) flags |= 8u;
  if (c.accelerator_affinity_supported()) flags |= 16u;
  w.u32(flags);
  write_provenance(w, c.provenance());
}

void read_capability(Reader& r, CapabilitySet& c) {
  c.set_generation(CapabilityGeneration(r.u64()));
  uint32_t flags = r.u32();
  c.set_rdma_capable((flags & 1u) != 0);
  c.set_sr_iov_capable((flags & 2u) != 0);
  c.set_offload_capable((flags & 4u) != 0);
  c.set_local_memory_observable((flags & 8u) != 0);
  c.set_accelerator_affinity_supported((flags & 16u) != 0);
  c.set_provenance(read_provenance(r));
}

void write_pci(Writer& w, const PciAddress& p) {
  w.u16(p.segment()); w.u8(p.bus()); w.u8(p.device()); w.u8(p.function());
}

PciAddress read_pci(Reader& r) {
  uint16_t seg = r.u16(); uint8_t bus = r.u8(); uint8_t dev = r.u8(); uint8_t fn = r.u8();
  return PciAddress(seg, bus, dev, fn);
}

void write_nic(Writer& w, const NicRecord& n) {
  w.u64(n.id.value()); w.u64(n.generation.value());
  w.str(n.os_name); w.str(n.os_description);
  write_pci(w, n.pci);
  w.u16(n.vendor_id); w.u16(n.device_id); w.u16(n.subsystem_vendor_id); w.u16(n.subsystem_id); w.u8(n.revision);
  w.u8(static_cast<uint8_t>(n.device_kind)); w.u8(static_cast<uint8_t>(n.function_class));
  write_capability(w, n.capability);
  w.u64(n.numa_node.value());
  write_provenance(w, n.provenance);
}

NicRecord read_nic(Reader& r) {
  NicRecord n;
  n.id = NicId(r.u64()); n.generation = NicGeneration(r.u64());
  n.os_name = r.str(); n.os_description = r.str();
  n.pci = read_pci(r);
  n.vendor_id = r.u16(); n.device_id = r.u16(); n.subsystem_vendor_id = r.u16(); n.subsystem_id = r.u16(); n.revision = r.u8();
  uint8_t dk = r.u8(); uint8_t fc = r.u8();
  if (dk > static_cast<uint8_t>(DeviceKind::kUnsupported)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid device kind");
  if (fc > static_cast<uint8_t>(FunctionClass::kUnknown)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid function class");
  n.device_kind = static_cast<DeviceKind>(dk); n.function_class = static_cast<FunctionClass>(fc);
  read_capability(r, n.capability);
  n.numa_node = NumaNodeId(r.u64());
  n.provenance = read_provenance(r);
  // Dynamic fields are NOT persisted as authority.
  n.link_state = LinkState::kUnknown;
  n.health = HealthState::kUnknown;
  n.lifecycle = LifecycleState::kRevalidationRequired;
  n.freshness = 0;
  return n;
}

void write_gpu(Writer& w, const GpuRecord& g) {
  w.u64(g.id.value()); w.u64(g.generation.value()); w.str(g.name); write_pci(w, g.pci);
  w.u64(g.cuda_device.value()); w.u8(g.cuda_verified ? 1 : 0); w.u64(g.numa_node.value());
  write_provenance(w, g.provenance);
}

GpuRecord read_gpu(Reader& r) {
  GpuRecord g;
  g.id = GpuId(r.u64()); g.generation = GpuGeneration(r.u64()); g.name = r.str(); g.pci = read_pci(r);
  g.cuda_device = DeviceId(r.u64()); g.cuda_verified = r.u8() != 0; g.numa_node = NumaNodeId(r.u64());
  g.provenance = read_provenance(r);
  g.freshness = 0;
  return g;
}

void serialize(const PersistentState& st, std::string& out) {
  Writer w;
  w.u32(st.format_version);
  w.u64(st.epoch.value());
  uint32_t pflag = 0;
  if (st.policy.require_rdma) pflag |= 1u;
  if (st.policy.require_sriov) pflag |= 2u;
  if (st.policy.require_offload) pflag |= 4u;
  if (st.policy.require_local_memory) pflag |= 8u;
  if (st.policy.require_same_numa) pflag |= 16u;
  if (st.policy.require_same_root_complex) pflag |= 32u;
  if (st.policy.require_same_pcie_switch) pflag |= 64u;
  if (st.policy.require_ready) pflag |= 128u;
  if (st.policy.require_link_up) pflag |= 256u;
  if (st.policy.allow_synthetic) pflag |= 512u;
  w.u32(pflag);
  w.u64(st.policy.freshness_limit);

  std::vector<NicRecord> nics = st.nics; std::sort(nics.begin(), nics.end(), [](const NicRecord& a, const NicRecord& b){ return a.id.value() < b.id.value(); });
  if (nics.size() > PersistenceStore::kMaxNics) throw ResidencyError(ErrorCode::kResourceLimit, "NIC count exceeds persistence bound");
  w.u32(static_cast<uint32_t>(nics.size()));
  for (const auto& n : nics) write_nic(w, n);

  std::vector<GpuRecord> gpus = st.gpus; std::sort(gpus.begin(), gpus.end(), [](const GpuRecord& a, const GpuRecord& b){ return a.id.value() < b.id.value(); });
  if (gpus.size() > PersistenceStore::kMaxGpus) throw ResidencyError(ErrorCode::kResourceLimit, "GPU count exceeds persistence bound");
  w.u32(static_cast<uint32_t>(gpus.size()));
  for (const auto& g : gpus) write_gpu(w, g);

  std::vector<ResidencyRecord> hist = st.history; std::sort(hist.begin(), hist.end(), [](const ResidencyRecord& a, const ResidencyRecord& b){ return a.id.value() < b.id.value(); });
  if (hist.size() > PersistenceStore::kMaxHistory) throw ResidencyError(ErrorCode::kResourceLimit, "history exceeds persistence bound");
  w.u32(static_cast<uint32_t>(hist.size()));
  for (const auto& r : hist) {
    w.u64(r.id.value()); w.u64(r.generation.value()); w.u64(r.nic.value()); w.u64(r.function.value()); w.u64(r.port.value()); w.u64(r.queue.value()); w.u64(r.target_gpu.value()); w.u64(r.target_numa.value()); w.u64(r.target_endpoint.value());
    w.u8(static_cast<uint8_t>(r.state)); w.u64(r.topology_generation.value());
    w.u8(static_cast<uint8_t>(r.ownership)); w.u64(r.owner_worker.value()); w.u64(r.owner_boot.value()); w.u64(r.epoch.value());
    w.u8(static_cast<uint8_t>(r.lifecycle)); w.str(r.explanation);
  }
  out.swap(w.buf);
}

void deserialize(const std::string& in, PersistentState& st) {
  Reader r(in);
  st.format_version = r.u32();
  if (st.format_version > kPersistenceFormatVersion) {
    throw ResidencyError(ErrorCode::kUnsupported, "unsupported future persistence format", std::to_string(st.format_version));
  }
  st.epoch = CoordinatorEpoch(r.u64());
  uint32_t pflag = r.u32();
  st.policy.require_rdma = (pflag & 1u) != 0;
  st.policy.require_sriov = (pflag & 2u) != 0;
  st.policy.require_offload = (pflag & 4u) != 0;
  st.policy.require_local_memory = (pflag & 8u) != 0;
  st.policy.require_same_numa = (pflag & 16u) != 0;
  st.policy.require_same_root_complex = (pflag & 32u) != 0;
  st.policy.require_same_pcie_switch = (pflag & 64u) != 0;
  st.policy.require_ready = (pflag & 128u) != 0;
  st.policy.require_link_up = (pflag & 256u) != 0;
  st.policy.allow_synthetic = (pflag & 512u) != 0;
  st.policy.freshness_limit = r.u64();
  uint32_t nics = r.u32();
  if (nics > PersistenceStore::kMaxNics) throw ResidencyError(ErrorCode::kIntegrityFailure, "NIC count too large");
  std::vector<NicId> seen;
  for (uint32_t i = 0; i < nics; ++i) {
    NicRecord n = read_nic(r);
    if (!n.id.valid()) throw ResidencyError(ErrorCode::kIntegrityFailure, "persisted NIC has invalid id");
    for (const auto& s : seen) if (s == n.id) throw ResidencyError(ErrorCode::kIntegrityFailure, "duplicate persisted NIC id");
    seen.push_back(n.id);
    st.nics.push_back(std::move(n));
  }
  uint32_t gpus = r.u32();
  if (gpus > PersistenceStore::kMaxGpus) throw ResidencyError(ErrorCode::kIntegrityFailure, "GPU count too large");
  std::vector<GpuId> seenG;
  for (uint32_t i = 0; i < gpus; ++i) {
    GpuRecord g = read_gpu(r);
    if (!g.id.valid()) throw ResidencyError(ErrorCode::kIntegrityFailure, "persisted GPU has invalid id");
    for (const auto& s : seenG) if (s == g.id) throw ResidencyError(ErrorCode::kIntegrityFailure, "duplicate persisted GPU id");
    seenG.push_back(g.id);
    st.gpus.push_back(std::move(g));
  }
  uint32_t hist = r.u32();
  if (hist > PersistenceStore::kMaxHistory) throw ResidencyError(ErrorCode::kIntegrityFailure, "history too large");
  for (uint32_t i = 0; i < hist; ++i) {
    ResidencyRecord rr;
    rr.id = ResidencyId(r.u64()); rr.generation = ResidencyGeneration(r.u64()); rr.nic = NicId(r.u64()); rr.function = FunctionId(r.u64()); rr.port = PortId(r.u64()); rr.queue = QueueId(r.u64()); rr.target_gpu = GpuId(r.u64()); rr.target_numa = NumaNodeId(r.u64()); rr.target_endpoint = EndpointId(r.u64());
    uint8_t s = r.u8(); if (s > static_cast<uint8_t>(ResidencyState::kRetired)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid residency state"); rr.state = static_cast<ResidencyState>(s);
    rr.topology_generation = TopologyGeneration(r.u64());
    uint8_t o = r.u8(); if (o > static_cast<uint8_t>(OwnershipState::kUnknown)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid ownership"); rr.ownership = static_cast<OwnershipState>(o);
    rr.owner_worker = WorkerId(r.u64()); rr.owner_boot = WorkerBootId(r.u64()); rr.epoch = CoordinatorEpoch(r.u64());
    uint8_t lc = r.u8(); if (lc > static_cast<uint8_t>(LifecycleState::kRetired)) throw ResidencyError(ErrorCode::kIntegrityFailure, "invalid lifecycle"); rr.lifecycle = static_cast<LifecycleState>(lc);
    rr.explanation = r.str();
    st.history.push_back(std::move(rr));
  }
  if (!r.at_end()) throw ResidencyError(ErrorCode::kIntegrityFailure, "trailing garbage after persistence payload");
}

}  // namespace

bool PersistenceStore::exists() const {
  std::ifstream f(path_, std::ios::binary);
  return f.good();
}

void PersistenceStore::save(const PersistentState& state) const {
  std::string payload;
  serialize(state, payload);
  std::string file;
  file.append(kMagicBox, kMagicLen);
  file.append(payload);
  uint64_t sum = fnv1a(payload);
  for (int i = 0; i < 8; ++i) file.push_back(static_cast<char>((sum >> (8 * i)) & 0xFF));
  std::string tmp = path_ + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw ResidencyError(ErrorCode::kBackendError, "cannot open persistence temp file", tmp);
    f.write(file.data(), static_cast<std::streamsize>(file.size()));
    f.flush();
    if (!f.good()) throw ResidencyError(ErrorCode::kBackendError, "persistence write failed", tmp);
  }
  // Atomic replacement over the target.  std::rename cannot overwrite an
  // existing file on Windows, so use MoveFileExA with REPLACE_EXISTING there
  // (which also guarantees the write-through of the flushed temp file).
  #ifdef _WIN32
  if (::MoveFileExA(tmp.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    std::remove(tmp.c_str());
    throw ResidencyError(ErrorCode::kBackendError, "persistence atomic replace failed", path_);
  }
  #else
  if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
    std::remove(tmp.c_str());
    throw ResidencyError(ErrorCode::kBackendError, "persistence atomic replace failed", path_);
  }
  #endif
}

PersistentState PersistenceStore::load() const {
  std::ifstream f(path_, std::ios::binary);
  if (!f) throw ResidencyError(ErrorCode::kNotFound, "persistence file not found", path_);
  std::ostringstream ss; ss << f.rdbuf();
  std::string data = ss.str();
  if (data.size() < kMagicLen + 8) throw ResidencyError(ErrorCode::kIntegrityFailure, "persistence file too short");
  if (std::memcmp(data.data(), kMagicBox, kMagicLen) != 0) throw ResidencyError(ErrorCode::kIntegrityFailure, "bad persistence magic");
  std::size_t payloadLen = data.size() - kMagicLen - 8;
  std::string payload = data.substr(kMagicLen, payloadLen);
  uint64_t sum = 0; for (int i = 0; i < 8; ++i) sum |= (static_cast<uint64_t>(static_cast<uint8_t>(data[data.size() - 8 + i])) << (8 * i));
  if (fnv1a(payload) != sum) throw ResidencyError(ErrorCode::kIntegrityFailure, "persistence checksum mismatch");
  PersistentState st;
  deserialize(payload, st);
  return st;
}

RecoveryResult PersistenceStore::recover() const {
  RecoveryResult res;
  if (!exists()) { res.message = "no persisted state"; return res; }
  try {
    PersistentState st = load();
    res.ok = true;
    res.epoch = st.epoch;
    res.nics = std::move(st.nics);
    res.gpus = std::move(st.gpus);
    res.policy = st.policy;
    res.historical_decisions = st.history.size();
    res.message = "recovered durable identity; dynamic evidence requires revalidation";
  } catch (const ResidencyError& e) {
    res.ok = false;
    res.message = std::string("recovery failed: ") + e.what();
  }
  return res;
}

}  // namespace nicresidency
