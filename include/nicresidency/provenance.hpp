#pragma once
// Provenance / evidence labeling.
//
// Every hardware-facing record carries one of REAL / SYNTHETIC / UNSUPPORTED.
// Provenance is preserved end-to-end and never silently changes kind across a
// reload or a derivation.  Synthetic state must never become REAL, and
// UNSUPPORTED must never become supported without wholly new evidence.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace nicresidency {

enum class EvidenceKind {
  kReal,        // observed from the real system / backend
  kSynthetic,   // synthetic fixture; never to be presented as real
  kUnsupported, // not supported by the current backend / platform
};

inline const char* to_string(EvidenceKind k) noexcept {
  switch (k) {
    case EvidenceKind::kReal: return "REAL";
    case EvidenceKind::kSynthetic: return "SYNTHETIC";
    case EvidenceKind::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// Additional provenance source tags.
enum class Source {
  kOperatingSystem,
  kPci,
  kNuma,
  kCuda,
  kNvml,
  kRdmaProvider,
  kVerbs,
  kNetAdapterApi,
  kVendorSdk,
  kDpuRuntime,
  kPersisted,
  kDerived,
  kSyntheticFixture,
};

inline const char* to_string(Source s) noexcept {
  switch (s) {
    case Source::kOperatingSystem: return "OPERATING_SYSTEM";
    case Source::kPci: return "PCI";
    case Source::kNuma: return "NUMA";
    case Source::kCuda: return "CUDA";
    case Source::kNvml: return "NVML";
    case Source::kRdmaProvider: return "RDMA_PROVIDER";
    case Source::kVerbs: return "VERBS";
    case Source::kNetAdapterApi: return "NET_ADAPTER_API";
    case Source::kVendorSdk: return "VENDOR_SDK";
    case Source::kDpuRuntime: return "DPU_RUNTIME";
    case Source::kPersisted: return "PERSISTED";
    case Source::kDerived: return "DERIVED";
    case Source::kSyntheticFixture: return "SYNTHETIC_FIXTURE";
  }
  return "UNKNOWN";
}

// A provenance record describes the class and origin of one piece of evidence.
// It is deliberately a value type: it can be copied, compared, and persisted.
class Provenance {
 public:
  Provenance() = default;

  explicit Provenance(EvidenceKind kind) : kind_(kind) {}

  Provenance(EvidenceKind kind, Source source) : kind_(kind) { sources_.push_back(source); }

  Provenance(EvidenceKind kind, std::vector<Source> sources)
      : kind_(kind), sources_(std::move(sources)) {}

  EvidenceKind kind() const noexcept { return kind_; }
  const std::vector<Source>& sources() const noexcept { return sources_; }

  bool has_source(Source s) const noexcept {
    for (Source x : sources_) {
      if (x == s) return true;
    }
    return false;
  }

  void add_source(Source s) {
    if (!has_source(s)) sources_.push_back(s);
  }

  // Only REAL evidence is admissible as current authority for live state.
  // SYNTHETIC and UNSUPPORTED are non-authoritative for real residency.
  bool authoritative() const noexcept { return kind_ == EvidenceKind::kReal; }
  bool synthetic() const noexcept { return kind_ == EvidenceKind::kSynthetic; }
  bool unsupported() const noexcept { return kind_ == EvidenceKind::kUnsupported; }

  // Exceeds the ever-changing set of sources and preserves order.
  bool operator==(const Provenance& o) const noexcept {
    if (kind_ != o.kind_) return false;
    if (sources_.size() != o.sources_.size()) return false;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      if (sources_[i] != o.sources_[i]) return false;
    }
    return true;
  }

  std::string str() const {
    std::string out = to_string(kind_);
    out += "[";
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      if (i) out += ",";
      out += to_string(sources_[i]);
    }
    out += "]";
    return out;
  }

 private:
  EvidenceKind kind_{EvidenceKind::kUnsupported};
  std::vector<Source> sources_;
};

inline std::ostream& operator<<(std::ostream& os, const Provenance& p) {
  return os << p.str();
}

}  // namespace nicresidency
