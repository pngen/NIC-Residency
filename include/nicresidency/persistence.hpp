#pragma once
// Durable state persistence with checked serialization, integrity checks and
// atomic replacement.  Only architecturally durable identity is persisted as
// current state; dynamic readiness / authority / attachment evidence is never
// persisted as fresh authority and always requires revalidation after reload.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>
#include <vector>

#include "nicresidency/id.hpp"
#include "nicresidency/records.hpp"

namespace nicresidency {

// Durable policy configuration.  Represents only static policy knowledge.
struct PersistedPolicy {
  bool require_rdma{false};
  bool require_sriov{false};
  bool require_offload{false};
  bool require_local_memory{false};
  bool require_same_numa{false};
  bool require_same_root_complex{false};
  bool require_same_pcie_switch{false};
  bool require_ready{false};
  bool require_link_up{false};
  bool allow_synthetic{false};
  std::uint64_t freshness_limit{0};
};

// The durable state that may be written to storage and recovered.  Dynamic
// evidence (link, health, ownership, attachment, residency) is deliberately
// excluded from the current-authority portion.
struct PersistentState {
  std::uint32_t format_version{1};
  CoordinatorEpoch epoch;
  std::vector<NicRecord> nics;
  std::vector<GpuRecord> gpus;
  PersistedPolicy policy;
  std::vector<ResidencyRecord> history;
};

// Result of a recovery: what was restored as durable, and what must remain
// non-authoritative until revalidated.
struct RecoveryResult {
  bool ok{false};
  CoordinatorEpoch epoch;
  std::vector<NicRecord> nics;
  std::vector<GpuRecord> gpus;
  PersistedPolicy policy;
  std::size_t historical_decisions{0};
  std::string message;
};

// A checked, atomic persistence store.
class PersistenceStore {
 public:
  explicit PersistenceStore(std::string path) : path_(std::move(path)) {}

  // Serialize (with integrity) and atomically replace the file.  Throws on
  // write failure; never leaves a partial file at path_.
  void save(const PersistentState& state) const;

  // Load and fully validate.  Throws ResidencyError(kIntegrityFailure) on any
  // corruption, truncation, trailing garbage, invalid enum/version, or
  // duplicate identity.
  PersistentState load() const;

  // Load and conservatively recover durable identity, marking all dynamic
  // evidence non-authoritative (REVALIDATION_REQUIRED).  Never throws for
  // "file absent" (returns ok=false in that case).
  RecoveryResult recover() const;

  bool exists() const;
  const std::string& path() const noexcept { return path_; }

  static constexpr std::uint32_t kMaxNics = 4096;
  static constexpr std::uint32_t kMaxGpus = 1024;
  static constexpr std::uint32_t kMaxHistory = 8192;
  static constexpr std::size_t kMaxFieldBytes = 1024 * 1024;

 private:
  std::string path_;
};

}  // namespace nicresidency
