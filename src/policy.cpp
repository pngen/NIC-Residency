
// Deterministic residency policy evaluation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/policy.hpp"

#include <algorithm>
#include <sstream>

namespace nicresidency {

namespace {

// Severity rank for choosing a representative overall eligibility when no
// candidate is allowed.  Lower rank is more severe.
int severity(ResidencyEligibility e) noexcept {
  switch (e) {
    case ResidencyEligibility::kStaleTopology: return 30;
    case ResidencyEligibility::kStaleDevice: return 31;
    case ResidencyEligibility::kStaleFunction: return 32;
    case ResidencyEligibility::kStaleQueue: return 33;
    case ResidencyEligibility::kStaleAttachment: return 34;
    case ResidencyEligibility::kRevalidationRequired: return 35;
    case ResidencyEligibility::kCapabilityMismatch: return 40;
    case ResidencyEligibility::kNotReady: return 41;
    case ResidencyEligibility::kNotLocal: return 42;
    case ResidencyEligibility::kPolicyRejected: return 43;
    case ResidencyEligibility::kInsufficientEvidence: return 44;
    case ResidencyEligibility::kUnsupported: return 45;
    case ResidencyEligibility::kNotFound: return 46;
    case ResidencyEligibility::kResidencyAllowed: return 1;
    case ResidencyEligibility::kResidencyAllowedDegraded: return 2;
  }
  return 100;
}

Locality locality_of(const RegistrySnapshot& reg, const NicRecord& nic, GpuId gpu,
                     const GpuRecord* gpu_rec, NumaNodeId target_numa) {
  (void)gpu;
  if (gpu_rec != nullptr) {
    Locality loc;
    if (reg.topology() != nullptr) {
      loc = reg.topology()->locality_between(nic.pci, gpu_rec->pci);
    } else {
      LocalityFactors f;
      bool same_numa0 = target_numa.valid() && nic.numa_node.valid() &&
                        nic.numa_node == target_numa;
      if (gpu_rec->numa_node.valid() && nic.numa_node.valid() &&
          gpu_rec->numa_node == nic.numa_node) {
        same_numa0 = true;
      }
      f.numa_same = same_numa0;
      f.cpu_local = same_numa0;
      f.accelerator_affine = same_numa0;
      f.cost = nic.pci.distance_to(gpu_rec->pci);
      loc = Locality(same_numa0 ? LocalityRelationship::kSameNumaNode
                                : LocalityRelationship::kUnknown,
                     same_numa0 ? nic.numa_node : NumaNodeId(), gpu_rec->pci,
                     Provenance(EvidenceKind::kReal, {Source::kNuma, Source::kPci, Source::kDerived}),
                     f);
    }
    // Real record-level NUMA equality (REAL[NUMA]) is always authoritative for
    // the numa-same factor, even when the derived topology is coarse.
    bool rec_same_numa =
        (gpu_rec->numa_node.valid() && nic.numa_node.valid() &&
         gpu_rec->numa_node == nic.numa_node) ||
        (target_numa.valid() && nic.numa_node.valid() && nic.numa_node == target_numa);
    LocalityFactors f = loc.factors();
    if (rec_same_numa) {
      f.numa_same = true;
      f.cpu_local = true;
      f.accelerator_affine = true;
      if (loc.relationship() == LocalityRelationship::kUnknown ||
          loc.relationship() == LocalityRelationship::kRemoteNuma ||
          loc.relationship() == LocalityRelationship::kHostLocal) {
        loc = Locality(LocalityRelationship::kSameNumaNode, nic.numa_node, gpu_rec->pci,
                       loc.provenance(), f);
      } else {
        loc = Locality(loc.relationship(), nic.numa_node, gpu_rec->pci, loc.provenance(), f);
      }
    } else {
      loc = Locality(loc.relationship(), loc.numa_node(), gpu_rec->pci, loc.provenance(), f);
    }
    return loc;
  }
  // GPU context absent: use the target NUMA node directly.
  LocalityFactors f;
  bool same_numa = target_numa.valid() && nic.numa_node.valid() && nic.numa_node == target_numa;
  f.numa_same = same_numa;
  f.cpu_local = same_numa;
  LocalityRelationship rel =
      same_numa ? LocalityRelationship::kSameNumaNode : LocalityRelationship::kUnknown;
  return Locality(rel, same_numa ? nic.numa_node : NumaNodeId(), nic.pci,
                  Provenance(EvidenceKind::kReal, {Source::kNuma, Source::kDerived}), f);
}

}  // namespace

ResidencyEligibility ResidencyPolicy::hard_check(const RegistrySnapshot& reg,
                                                 const NicRecord& nic, GpuId gpu,
                                                 NumaNodeId numa,
                                                 std::vector<RejectionReason>& reasons) const {
  (void)reg; (void)gpu;
  // SYNTHETIC / UNSUPPORTED evidence is not admissible as real residency.
  if (nic.provenance.synthetic() && !allow_synthetic_) {
    reasons.push_back({"SYNTHETIC", "synthetic evidence is not admissible for real residency"});
    return ResidencyEligibility::kInsufficientEvidence;
  }
  if (nic.provenance.unsupported()) {
    reasons.push_back({"UNSUPPORTED", "backend reported this capability as unsupported"});
    return ResidencyEligibility::kUnsupported;
  }

  if (excluded_.count(nic.id) != 0) {
    reasons.push_back({"EXCLUDED", "NIC is excluded by policy"});
    return ResidencyEligibility::kPolicyRejected;
  }

  if (!nic.id.valid()) {
    reasons.push_back({"NOT_FOUND", "invalid NIC id"});
    return ResidencyEligibility::kNotFound;
  }

  if (nic.lifecycle == LifecycleState::kRevalidationRequired ||
      nic.lifecycle == LifecycleState::kDetached) {
    reasons.push_back({"STALE_DEVICE", "device requires revalidation"});
    return ResidencyEligibility::kRevalidationRequired;
  }
  if (nic.lifecycle == LifecycleState::kFailed) {
    reasons.push_back({"NOT_READY", "device failed"});
    return ResidencyEligibility::kNotReady;
  }
  if (nic.lifecycle == LifecycleState::kRetired) {
    reasons.push_back({"NOT_FOUND", "device retired"});
    return ResidencyEligibility::kNotFound;
  }
  if (nic.lifecycle == LifecycleState::kDiscovered) {
    reasons.push_back({"NOT_READY", "device only discovered, not yet available"});
    return ResidencyEligibility::kNotReady;
  }

  if (require_rdma_ && !nic.capability.rdma_capable()) {
    reasons.push_back({"CAPABILITY_RDMA", "REQUIRED RDMA capability absent"});
    return ResidencyEligibility::kCapabilityMismatch;
  }
  if (require_sriov_ && !nic.capability.sr_iov_capable()) {
    reasons.push_back({"CAPABILITY_SRIOV", "REQUIRED SR-IOV capability absent"});
    return ResidencyEligibility::kCapabilityMismatch;
  }
  if (require_offload_ && !nic.capability.offload_capable()) {
    reasons.push_back({"CAPABILITY_OFFLOAD", "REQUIRED offload capability absent"});
    return ResidencyEligibility::kCapabilityMismatch;
  }
  if (require_local_memory_ && !nic.capability.local_memory_observable()) {
    reasons.push_back({"CAPABILITY_LOCAL_MEMORY", "REQUIRED local-memory capability absent"});
    return ResidencyEligibility::kCapabilityMismatch;
  }

  if (require_ready_ && nic.health != HealthState::kReady) {
    reasons.push_back({"NOT_READY", "device health is not READY"});
    return ResidencyEligibility::kNotReady;
  }
  if (require_link_up_ && nic.link_state != LinkState::kUp) {
    reasons.push_back({"NOT_READY_LINK", "link is not UP"});
    return ResidencyEligibility::kNotReady;
  }

  if (nic.freshness > freshness_limit_) {
    reasons.push_back({"STALE_FRESHNESS", "device evidence is too old"});
    return ResidencyEligibility::kRevalidationRequired;
  }

  // Locality constraints (hard).
  const GpuRecord* gpu_rec = gpu.valid() ? reg.gpu(gpu) : nullptr;
  Locality loc = locality_of(reg, nic, gpu, gpu_rec, numa);

  bool same_numa = loc.factors().numa_same ||
                   (numa.valid() && nic.numa_node.valid() && nic.numa_node == numa);
  if (require_same_numa_ && !same_numa) {
    reasons.push_back({"NOT_LOCAL_NUMA", "NIC is not on the required NUMA node"});
    return ResidencyEligibility::kNotLocal;
  }
  if (require_same_rc_ && !loc.factors().root_complex_same) {
    reasons.push_back({"NOT_LOCAL_ROOT_COMPLEX", "NIC is not on the required root complex"});
    return ResidencyEligibility::kNotLocal;
  }
  if (require_same_switch_ && !loc.factors().pcie_switch_same) {
    reasons.push_back({"NOT_LOCAL_SWITCH", "NIC is not on the required PCIe switch"});
    return ResidencyEligibility::kNotLocal;
  }

  // Degraded?  A remote-NUMA relationship with no stronger locality is only
  // eligible in degraded form.
  if (loc.relationship() == LocalityRelationship::kRemoteNuma ||
      loc.relationship() == LocalityRelationship::kUnknown ||
      loc.relationship() == LocalityRelationship::kHostLocal) {
    return ResidencyEligibility::kResidencyAllowedDegraded;
  }
  return ResidencyEligibility::kResidencyAllowed;
}

std::int64_t ResidencyPolicy::rank(const RegistrySnapshot& reg, const NicRecord& nic,
                                   const RegistrySnapshot& snap, GpuId gpu,
                                   NumaNodeId numa) const {
  (void)snap;
  const GpuRecord* gpu_rec = gpu.valid() ? reg.gpu(gpu) : nullptr;
  Locality loc = locality_of(reg, nic, gpu, gpu_rec, numa);
  const LocalityFactors& f = loc.factors();

  std::int64_t score = 0;
  if (f.pcie_switch_same) score += w_same_switch_;
  if (f.root_complex_same) score += w_same_root_;
  if (f.numa_same) score += w_same_numa_;
  if (f.accelerator_affine) score += w_accel_;
  if (f.cpu_local) score += w_cpu_local_;
  if (f.local_memory) score += w_local_mem_;
  // Capability completeness: a NIC with more observed capabilities is not
  // automatically better; but a strictly missing required capability was
  // already rejected.  A small positive bonus rewards richer evidence.
  int cap_count = 0;
  if (nic.capability.rdma_capable()) ++cap_count;
  if (nic.capability.sr_iov_capable()) ++cap_count;
  if (nic.capability.offload_capable()) ++cap_count;
  if (nic.capability.local_memory_observable()) ++cap_count;
  if (nic.capability.accelerator_affinity_supported()) ++cap_count;
  score += w_cap_ * static_cast<std::int64_t>(cap_count);
  // Add a small deterministic refinement from the locality cost so that ties
  // at the named-factor level still order by a stable numeric key.
  return score;
}

ResidencyDecision ResidencyPolicy::decide(const RegistrySnapshot& reg, GpuId target_gpu,
                                          NumaNodeId target_numa, EndpointId endpoint,
                                          WorkerId workload) const {
  ResidencyDecision d;
  d.id = DecisionId(1);
  d.workload = workload;
  d.target_gpu = target_gpu;
  d.target_numa = target_numa;
  d.target_endpoint = endpoint;
  if (reg.topology() != nullptr) d.topology_generation = reg.topology()->generation();
  d.policy = policy_id_;
  d.epoch = reg.epoch();

  const GpuRecord* gpu_rec = target_gpu.valid() ? reg.gpu(target_gpu) : nullptr;

  // Collect evaluated candidates, subject to bound.
  std::size_t considered = 0;
  std::vector<CandidateResult> allowed;

  for (const auto& kv : reg.nics()) {
    const NicRecord& nic = kv.second;
    if (considered >= max_candidates_) break;
    ++considered;

    CandidateResult cr;
    cr.nic = nic.id;
    cr.eligibility = hard_check(reg, nic, target_gpu, target_numa, cr.rejections);
    Locality loc = locality_of(reg, nic, target_gpu, gpu_rec, target_numa);
    cr.locality_summary = loc.str();

    if (cr.eligibility == ResidencyEligibility::kResidencyAllowed ||
        cr.eligibility == ResidencyEligibility::kResidencyAllowedDegraded) {
      cr.rank_score = rank(reg, nic, reg, target_gpu, target_numa);
      allowed.push_back(cr);
      d.candidates.push_back(nic.id);
    }
    d.evaluated.push_back(std::move(cr));
  }

  // Stable sort: strict total order on (rank_score, nic.id).
  std::sort(allowed.begin(), allowed.end(), [](const CandidateResult& a, const CandidateResult& b) {
    if (a.rank_score != b.rank_score) return a.rank_score < b.rank_score;
    return a.nic.value() < b.nic.value();
  });

  if (!allowed.empty()) {
    const CandidateResult& top = allowed.front();
    d.selected = top.nic;
    d.selected_function = top.function;
    d.selected_port = top.port;
    d.selected_queue = top.queue;
    d.overall = top.eligibility;
    // Mark the selected candidate in the full evaluated list.
    for (auto& e : d.evaluated) {
      if (e.nic == top.nic) {
        e.selected = true;
        e.eligibility = top.eligibility;
      }
    }
    // Record generations at selection time.
    const NicRecord* sel = reg.nic(d.selected);
    if (sel != nullptr) {
      d.nic_generation = sel->generation;
      const FunctionRecord* fr = d.selected_function.valid() ? reg.function(d.selected_function) : nullptr;
      if (fr != nullptr) d.function_generation = fr->generation;
      const QueueRecord* qr = d.selected_queue.valid() ? reg.queue(d.selected_queue) : nullptr;
      if (qr != nullptr) d.queue_generation = qr->generation;
    }
    d.explanation = build_explanation(d, reg);
  } else {
    // No candidate allowed: represent the most severe rejection.
    d.overall = ResidencyEligibility::kNotFound;
    if (!d.evaluated.empty()) {
      ResidencyEligibility worst = d.evaluated.front().eligibility;
      for (const auto& e : d.evaluated) {
        if (severity(e.eligibility) < severity(worst)) worst = e.eligibility;
      }
      d.overall = worst;
    }
    d.explanation = build_explanation(d, reg);
  }
  return d;
}

std::string ResidencyPolicy::build_explanation(const ResidencyDecision& d,
                                               const RegistrySnapshot& reg) const {
  (void)reg;
  std::ostringstream os;
  if (d.selected.valid()) {
    os << "selected=" << d.selected.str();
    if (d.selected_function.valid()) os << " func=" << d.selected_function.str();
    if (d.selected_port.valid()) os << " port=" << d.selected_port.str();
    if (d.selected_queue.valid()) os << " queue=" << d.selected_queue.str();
    os << " via " << to_string(d.overall);
  } else {
    os << "no candidate selected; overall=" << to_string(d.overall);
  }
  return os.str();
}

}  // namespace nicresidency
