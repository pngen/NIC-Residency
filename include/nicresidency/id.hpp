#pragma once
// Strongly-typed identity framework for NIC Residency.
//
// Distinct identity categories are distinct C++ types via the Tag template
// parameter.  Raw integers or strings must never be interchanged between
// different identity categories; the type system enforces this at compile time.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

namespace nicresidency {

namespace detail {

// Render an unsigned 64-bit value as zero-padded fixed-width hex.
inline std::string hex64(std::uint64_t v) {
  static const char* kDigits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kDigits[v & 0xF];
    v >>= 4;
  }
  return out;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Id<Tag> - a stable, typed opaque identifier.
//   * value() == 0 denotes the invalid / null id.
//   * The u64 value is stable across process incarnations when the source
//     backend provides a stable identity (e.g. a hashed MAC, PCI BDF, GUID).
// ---------------------------------------------------------------------------
template <class Tag>
class Id {
 public:
  using value_type = std::uint64_t;
  static_assert(std::is_class<Tag>::value || std::is_enum<Tag>::value,
                "Id<Tag> requires a distinct tag type");

  constexpr Id() noexcept = default;
  constexpr explicit Id(value_type value) noexcept : value_(value) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool valid() const noexcept { return value_ != 0; }

  std::string str() const { return detail::hex64(value_); }

  friend constexpr bool operator==(const Id& a, const Id& b) noexcept {
    return a.value_ == b.value_;
  }
  friend constexpr bool operator!=(const Id& a, const Id& b) noexcept {
    return !(a == b);
  }
  friend constexpr bool operator<(const Id& a, const Id& b) noexcept {
    return a.value_ < b.value_;
  }
  friend constexpr bool operator>(const Id& a, const Id& b) noexcept {
    return b < a;
  }
  friend constexpr bool operator<=(const Id& a, const Id& b) noexcept {
    return !(b < a);
  }
  friend constexpr bool operator>=(const Id& a, const Id& b) noexcept {
    return !(a < b);
  }

 private:
  value_type value_{0};  // 0 == invalid/null id
};

// ---------------------------------------------------------------------------
// Generation<Tag> - a monotonic, typed generation counter.
//   * Generations never decrease (invariant).
//   * Coalesce/merge uses the maximum; next() advances by exactly one.
// ---------------------------------------------------------------------------
template <class Tag>
class Generation {
 public:
  using value_type = std::uint64_t;

  constexpr Generation() noexcept = default;
  constexpr explicit Generation(value_type value) noexcept : value_(value) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool valid() const noexcept { return value_ != 0; }

  constexpr Generation next() const noexcept {
    return Generation(static_cast<value_type>(value_ + 1));
  }

  static constexpr Generation merge(const Generation& a, const Generation& b) noexcept {
    return a.value_ >= b.value_ ? a : b;
  }

  std::string str() const { return std::to_string(value_); }

  friend constexpr bool operator==(const Generation& a, const Generation& b) noexcept {
    return a.value_ == b.value_;
  }
  friend constexpr bool operator!=(const Generation& a, const Generation& b) noexcept {
    return !(a == b);
  }
  friend constexpr bool operator<(const Generation& a, const Generation& b) noexcept {
    return a.value_ < b.value_;
  }
  friend constexpr bool operator>(const Generation& a, const Generation& b) noexcept {
    return b < a;
  }
  friend constexpr bool operator<=(const Generation& a, const Generation& b) noexcept {
    return !(b < a);
  }
  friend constexpr bool operator>=(const Generation& a, const Generation& b) noexcept {
    return !(a < b);
  }

 private:
  value_type value_{0};
};

// Identity category tags.
struct NicTag {};
struct NicGenTag {};
struct PortTag {};
struct PortGenTag {};
struct FunctionTag {};
struct FunctionGenTag {};
struct QueueTag {};
struct QueueGenTag {};
struct GpuTag {};
struct GpuGenTag {};
struct DeviceTag {};
struct DeviceGenTag {};
struct NumaNodeTag {};
struct NumaNodeGenTag {};
struct PciNodeTag {};
struct PciNodeGenTag {};
struct EndpointTag {};
struct EndpointGenTag {};
struct ResidencyTag {};
struct ResidencyGenTag {};
struct WorkerTag {};
struct WorkerBootTag {};
struct CoordinatorEpochTag {};
struct ObservationTag {};
struct TopologyGenTag {};
struct CapabilityGenTag {};
struct PolicyTag {};
struct DecisionTag {};
struct AttachmentTag {};
struct AttachmentGenTag {};

// Public aliases.
using NicId = Id<NicTag>;
using NicGeneration = Generation<NicGenTag>;
using PortId = Id<PortTag>;
using PortGeneration = Generation<PortGenTag>;
using FunctionId = Id<FunctionTag>;
using FunctionGeneration = Generation<FunctionGenTag>;
using QueueId = Id<QueueTag>;
using QueueGeneration = Generation<QueueGenTag>;
using GpuId = Id<GpuTag>;
using GpuGeneration = Generation<GpuGenTag>;
using DeviceId = Id<DeviceTag>;
using DeviceGeneration = Generation<DeviceGenTag>;
using NumaNodeId = Id<NumaNodeTag>;
using NumaNodeGeneration = Generation<NumaNodeGenTag>;
using PciNodeId = Id<PciNodeTag>;
using PciNodeGeneration = Generation<PciNodeGenTag>;
using EndpointId = Id<EndpointTag>;
using EndpointGeneration = Generation<EndpointGenTag>;
using WorkerId = Id<WorkerTag>;
using WorkerBootId = Id<WorkerBootTag>;
using CoordinatorEpoch = Id<CoordinatorEpochTag>;
using ObservationId = Id<ObservationTag>;
using TopologyGeneration = Generation<TopologyGenTag>;
using CapabilityGeneration = Generation<CapabilityGenTag>;
using PolicyId = Id<PolicyTag>;
using DecisionId = Id<DecisionTag>;
using AttachmentId = Id<AttachmentTag>;
using AttachmentGeneration = Generation<AttachmentGenTag>;
using ResidencyId = Id<ResidencyTag>;
using ResidencyGeneration = Generation<ResidencyGenTag>;

}  // namespace nicresidency

namespace std {
template <class Tag>
struct hash<nicresidency::Id<Tag>> {
  std::size_t operator()(const nicresidency::Id<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
template <class Tag>
struct hash<nicresidency::Generation<Tag>> {
  std::size_t operator()(const nicresidency::Generation<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};
}  // namespace std
