#pragma once
// Explicit, explainable locality model.
//
// Locality is never a single opaque distance.  A named set of contributing
// factors is preserved, and every relationship is labeled with evidence.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <ostream>
#include <string>

#include "nicresidency/id.hpp"
#include "nicresidency/provenance.hpp"

namespace nicresidency {

// A PCI device address (BDF), used for stable PCI identity.  Stores the
// 16-bit bus:device.function under its canonical numeric parts so that the
// address can be compared and hashed deterministically.
class PciAddress {
 public:
  PciAddress() = default;
  constexpr PciAddress(std::uint16_t segment, std::uint8_t bus,
                       std::uint8_t device, std::uint8_t function) noexcept
      : segment_(segment), bus_(bus), device_(device), function_(function) {}

  static PciAddress from_bdf(std::uint16_t segment, std::uint16_t bdf) noexcept {
    return PciAddress(segment, static_cast<std::uint8_t>((bdf >> 8) & 0xFF),
                      static_cast<std::uint8_t>((bdf >> 3) & 0x1F),
                      static_cast<std::uint8_t>(bdf & 0x7));
  }

  std::uint16_t segment() const noexcept { return segment_; }
  std::uint8_t bus() const noexcept { return bus_; }
  std::uint8_t device() const noexcept { return device_; }
  std::uint8_t function() const noexcept { return function_; }

  constexpr bool valid() const noexcept { return bus_ != 0 || device_ != 0 || function_ != 0; }

  // The numeric ordering that makes BDFs comparable and stable.
  constexpr std::uint32_t numeric() const noexcept {
    return (static_cast<std::uint32_t>(segment_) << 16) |
           (static_cast<std::uint32_t>(bus_) << 8) |
           (static_cast<std::uint32_t>(device_) << 3) |
           static_cast<std::uint32_t>(function_);
  }

  std::int64_t distance_to(const PciAddress& other) const noexcept {
    // A cheap, deterministic proximity proxy: compare bus and segment.
    // A pure BDF-based distance is NOT a claim about PCIe topology; it is
    // only used as a stable tie-break key.  Topology relationship must be
    // derived from the PCI hierarchy, never from this raw number.
    const std::int64_t dbus =
        static_cast<std::int64_t>(bus()) - static_cast<std::int64_t>(other.bus());
    const std::int64_t dseg =
        static_cast<std::int64_t>(segment()) - static_cast<std::int64_t>(other.segment());
    return dseg * 1000 + dbus;
  }

  std::string str() const {
    char buf[13];
    const char* hex = "0123456789abcdef";
    int pos = 0;
    buf[pos++] = hex[(segment_ >> 12) & 0xF];
    buf[pos++] = hex[(segment_ >> 8) & 0xF];
    buf[pos++] = hex[(segment_ >> 4) & 0xF];
    buf[pos++] = hex[segment_ & 0xF];
    buf[pos++] = ':';
    buf[pos++] = hex[(bus_ >> 4) & 0xF];
    buf[pos++] = hex[bus_ & 0xF];
    buf[pos++] = ':';
    buf[pos++] = hex[(device_ >> 4) & 0xF];
    buf[pos++] = hex[device_ & 0xF];
    buf[pos++] = '.';
    buf[pos++] = hex[function_ & 0xF];
    buf[pos] = '\0';
    return std::string(buf);
  }

  friend constexpr bool operator==(const PciAddress& a, const PciAddress& b) noexcept {
    return a.numeric() == b.numeric();
  }
  friend constexpr bool operator!=(const PciAddress& a, const PciAddress& b) noexcept {
    return !(a == b);
  }
  friend constexpr bool operator<(const PciAddress& a, const PciAddress& b) noexcept {
    return a.numeric() < b.numeric();
  }

 private:
  std::uint16_t segment_{0};
  std::uint8_t bus_{0};
  std::uint8_t device_{0};
  std::uint8_t function_{0};
};

inline std::ostream& operator<<(std::ostream& os, const PciAddress& p) {
  return os << p.str();
}

// LocalityRelationships are the explicit, named relationships we model.  They
// are never deduced from a single opaque distance.
enum class LocalityRelationship {
  kSameDevice,
  kSamePcieSwitch,
  kSameRootComplex,
  kSameNumaNode,
  kRemoteNuma,
  kHostLocal,
  kAcceleratorLocal,
  kUnknown,
  kUnsupported,
};

inline const char* to_string(LocalityRelationship r) noexcept {
  switch (r) {
    case LocalityRelationship::kSameDevice: return "SAME_DEVICE";
    case LocalityRelationship::kSamePcieSwitch: return "SAME_PCIE_SWITCH";
    case LocalityRelationship::kSameRootComplex: return "SAME_ROOT_COMPLEX";
    case LocalityRelationship::kSameNumaNode: return "SAME_NUMA_NODE";
    case LocalityRelationship::kRemoteNuma: return "REMOTE_NUMA";
    case LocalityRelationship::kHostLocal: return "HOST_LOCAL";
    case LocalityRelationship::kAcceleratorLocal: return "ACCELERATOR_LOCAL";
    case LocalityRelationship::kUnknown: return "UNKNOWN";
    case LocalityRelationship::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// Named locality contributing factors.  Every numeric locality cost is
// accompanied by these named factors so that a decision can be explained.
struct LocalityFactors {
  bool root_complex_same{false};     // same PCIe root complex
  bool pcie_switch_same{false};      // share a PCIe switch ancestry
  bool numa_same{false};             // same NUMA node
  bool accelerator_affine{false};    // accelerator-affinity favorable at guest level
  bool cpu_local{false};             // CPU-local
  bool local_memory{false};          // NIC/DPU-local memory observable & local
  bool host_local{false};            // host-local as opposed to accelerator-local
  bool accelerator_local{false};     // accelerator-local (e.g. on-die/attached)
  bool direct_pci_neighbor{false};   // sibling PCI relationship under a common bus

  // Stable numeric cost.  Lower is preferred.  This is derived from factors
  // and is preserved as a named aggregate, never as the whole model.
  std::int64_t cost{0};

  bool operator==(const LocalityFactors& o) const noexcept {
    return root_complex_same == o.root_complex_same &&
           pcie_switch_same == o.pcie_switch_same &&
           numa_same == o.numa_same &&
           accelerator_affine == o.accelerator_affine &&
           cpu_local == o.cpu_local &&
           local_memory == o.local_memory &&
           host_local == o.host_local &&
           accelerator_local == o.accelerator_local &&
           direct_pci_neighbor == o.direct_pci_neighbor &&
           cost == o.cost;
  }
};

// A complete locality statement for one NIC/function/queue relative to a
// target compute/memory/accelerator domain.
class Locality {
 public:
  Locality() = default;

  Locality(LocalityRelationship rel, NumaNodeId numa, PciAddress pci,
           Provenance prov, LocalityFactors factors)
      : relationship_(rel), numa_node_(numa), pci_(pci),
        provenance_(std::move(prov)), factors_(factors) {}

  LocalityRelationship relationship() const noexcept { return relationship_; }
  NumaNodeId numa_node() const noexcept { return numa_node_; }
  const PciAddress& pci() const noexcept { return pci_; }
  const Provenance& provenance() const noexcept { return provenance_; }
  const LocalityFactors& factors() const noexcept { return factors_; }

  std::string str() const {
    std::string out = "REL=";
    out += to_string(relationship_);
    out += " NUMA=";
    if (numa_node_.valid()) {
      out += std::to_string(numa_node_.value());
    } else {
      out += "none";
    }
    out += " PCI=";
    out += pci_.str();
    out += " PROV=";
    out += provenance_.str();
    return out;
  }

 private:
  LocalityRelationship relationship_{LocalityRelationship::kUnknown};
  NumaNodeId numa_node_;
  PciAddress pci_;
  Provenance provenance_;
  LocalityFactors factors_;
};

inline std::ostream& operator<<(std::ostream& os, const Locality& l) {
  return os << l.str();
}

// Convenience: whether a relationship is "favorable enough to be investigated".
inline bool is_localised(LocalityRelationship r) noexcept {
  switch (r) {
    case LocalityRelationship::kSameDevice:
    case LocalityRelationship::kSamePcieSwitch:
    case LocalityRelationship::kSameRootComplex:
    case LocalityRelationship::kSameNumaNode:
    case LocalityRelationship::kAcceleratorLocal:
      return true;
    case LocalityRelationship::kRemoteNuma:
    case LocalityRelationship::kHostLocal:
    case LocalityRelationship::kUnknown:
    case LocalityRelationship::kUnsupported:
      return false;
  }
  return false;
}

}  // namespace nicresidency
