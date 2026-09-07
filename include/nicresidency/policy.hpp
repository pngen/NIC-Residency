#pragma once
// Deterministic residency policy: hard constraints first, then named ranking
// factors, then stable tie-breaking.  Insertion order must never change the
// result, so the final ordering is a strict total order on
// (rank_score, canonical_key) where canonical_key is the candidate NIC id.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "nicresidency/enums.hpp"
#include "nicresidency/id.hpp"
#include "nicresidency/registry.hpp"
#include "nicresidency/residency.hpp"

namespace nicresidency {

class ResidencyPolicy {
 public:
  ResidencyPolicy() = default;

  // --- hard-constraint configuration ---------------------------------------
  void require_capability_rdma(bool v) noexcept { require_rdma_ = v; }
  void require_capability_sriov(bool v) noexcept { require_sriov_ = v; }
  void require_capability_offload(bool v) noexcept { require_offload_ = v; }
  void require_local_memory(bool v) noexcept { require_local_memory_ = v; }
  void require_same_numa_node(bool v) noexcept { require_same_numa_ = v; }
  void require_same_root_complex(bool v) noexcept { require_same_rc_ = v; }
  void require_same_pcie_switch(bool v) noexcept { require_same_switch_ = v; }
  void require_ready(bool v) noexcept { require_ready_ = v; }
  void require_quality_link(bool v) noexcept { require_link_up_ = v; }
  void set_freshness_limit(std::uint64_t limit) noexcept { freshness_limit_ = limit; }
  void set_max_candidates(std::size_t max) noexcept { max_candidates_ = max; }
  // Allow SYNTHETIC evidence to be eligible.  For real residency this must be
  // left false (fail closed); synthetic scenarios set it true explicitly.
  void set_allow_synthetic(bool v) noexcept { allow_synthetic_ = v; }
  bool allow_synthetic() const noexcept { return allow_synthetic_; }
  void exclude_nic(NicId id) { excluded_.insert(id); }
  void set_policy_id(PolicyId id) noexcept { policy_id_ = id; }

  // --- ranking weights -----------------------------------------------------
  void set_weight_same_switch(std::int64_t w) noexcept { w_same_switch_ = w; }
  void set_weight_same_root_complex(std::int64_t w) noexcept { w_same_root_ = w; }
  void set_weight_same_numa(std::int64_t w) noexcept { w_same_numa_ = w; }
  void set_weight_accelerator_affinity(std::int64_t w) noexcept { w_accel_ = w; }
  void set_weight_cpu_local(std::int64_t w) noexcept { w_cpu_local_ = w; }
  void set_weight_local_memory(std::int64_t w) noexcept { w_local_mem_ = w; }
  void set_weight_capability_completeness(std::int64_t w) noexcept { w_cap_ = w; }

  // --- evaluation ----------------------------------------------------------
  ResidencyDecision decide(const RegistrySnapshot& reg, GpuId target_gpu,
                           NumaNodeId target_numa, EndpointId endpoint,
                           WorkerId workload) const;

 private:
  struct Pen {
    std::int64_t rank_score{0};
    NicId nic;
    FunctionId function;
    PortId port;
    QueueId queue;
  };

  std::int64_t rank(const RegistrySnapshot& reg, const NicRecord& nic,
                    const RegistrySnapshot& snap, GpuId gpu, NumaNodeId numa) const;
  ResidencyEligibility hard_check(const RegistrySnapshot& reg, const NicRecord& nic,
                                  GpuId gpu, NumaNodeId numa,
                                  std::vector<RejectionReason>& reasons) const;
  std::string build_explanation(const ResidencyDecision& d,
                                const RegistrySnapshot& reg) const;

  bool require_rdma_{false};
  bool require_sriov_{false};
  bool require_offload_{false};
  bool require_local_memory_{false};
  bool require_same_numa_{false};
  bool require_same_rc_{false};
  bool require_same_switch_{false};
  bool require_ready_{false};
  bool require_link_up_{false};
  std::uint64_t freshness_limit_{std::numeric_limits<std::uint64_t>::max()};
  std::size_t max_candidates_{1024};
  bool allow_synthetic_{false};

  std::set<NicId> excluded_;
  PolicyId policy_id_{PolicyId(1)};

  std::int64_t w_same_switch_{-40};
  std::int64_t w_same_root_{-20};
  std::int64_t w_same_numa_{-10};
  std::int64_t w_accel_{-50};
  std::int64_t w_cpu_local_{-5};
  std::int64_t w_local_mem_{-30};
  std::int64_t w_cap_{0};
};

}  // namespace nicresidency
