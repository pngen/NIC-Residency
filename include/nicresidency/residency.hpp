#pragma once
// Residency eligibility and decision model.
//
// The core thesis: a NIC being present does not mean it is the right NIC for a
// workload.  Eligibility is a typed outcome computed against a set of hard
// constraints (fail closed) and a deterministic set of named ranking factors.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>
#include <vector>

#include "nicresidency/error.hpp"
#include "nicresidency/id.hpp"
#include "nicresidency/records.hpp"

namespace nicresidency {

enum class ResidencyEligibility {
  kResidencyAllowed,
  kResidencyAllowedDegraded,
  kNotLocal,
  kNotReady,
  kStaleTopology,
  kStaleDevice,
  kStaleFunction,
  kStaleQueue,
  kStaleAttachment,
  kRevalidationRequired,
  kCapabilityMismatch,
  kPolicyRejected,
  kInsufficientEvidence,
  kUnsupported,
  kNotFound,
};

inline const char* to_string(ResidencyEligibility e) noexcept {
  switch (e) {
    case ResidencyEligibility::kResidencyAllowed: return "RESIDENCY_ALLOWED";
    case ResidencyEligibility::kResidencyAllowedDegraded: return "RESIDENCY_ALLOWED_DEGRADED";
    case ResidencyEligibility::kNotLocal: return "NOT_LOCAL";
    case ResidencyEligibility::kNotReady: return "NOT_READY";
    case ResidencyEligibility::kStaleTopology: return "STALE_TOPOLOGY";
    case ResidencyEligibility::kStaleDevice: return "STALE_DEVICE";
    case ResidencyEligibility::kStaleFunction: return "STALE_FUNCTION";
    case ResidencyEligibility::kStaleQueue: return "STALE_QUEUE";
    case ResidencyEligibility::kStaleAttachment: return "STALE_ATTACHMENT";
    case ResidencyEligibility::kRevalidationRequired: return "REVALIDATION_REQUIRED";
    case ResidencyEligibility::kCapabilityMismatch: return "CAPABILITY_MISMATCH";
    case ResidencyEligibility::kPolicyRejected: return "POLICY_REJECTED";
    case ResidencyEligibility::kInsufficientEvidence: return "INSUFFICIENT_EVIDENCE";
    case ResidencyEligibility::kUnsupported: return "UNSUPPORTED";
    case ResidencyEligibility::kNotFound: return "NOT_FOUND";
  }
  return "UNKNOWN";
}

// A named, inspectable rejection for one candidate.
struct RejectionReason {
  std::string name;    // stable token, e.g. "STALE_NIC"
  std::string detail;  // human-readable detail
};

// Per-candidate evaluation result.
struct CandidateResult {
  NicId nic;
  FunctionId function;      // optional
  PortId port;              // optional
  QueueId queue;            // optional
  ResidencyEligibility eligibility;
  std::int64_t rank_score{0};
  bool selected{false};
  std::vector<RejectionReason> rejections;
  std::string locality_summary;  // named locality facts for explanation
};

// One residency decision.  A decision created under generation N stops being
// authoritative once a relevant generation advances.
struct ResidencyDecision {
  DecisionId id;

  WorkerId workload;          // target workload context (optional)
  GpuId target_gpu;           // optional
  NumaNodeId target_numa;     // required context
  EndpointId target_endpoint; // optional

  NicId selected;             // invalid => nothing was selected
  FunctionId selected_function;
  PortId selected_port;
  QueueId selected_queue;

  TopologyGeneration topology_generation;
  NicGeneration nic_generation;
  FunctionGeneration function_generation;
  QueueGeneration queue_generation;
  AttachmentGeneration attachment_generation;
  CapabilityGeneration capability_generation;
  ResidencyGeneration residency_generation;

  PolicyId policy;
  std::vector<NicId> candidates;
  std::vector<CandidateResult> evaluated;
  ResidencyEligibility overall{ResidencyEligibility::kNotFound};

  CoordinatorEpoch epoch;
  WorkerBootId authority_boot;
  std::uint64_t freshness{0};
  std::string explanation;

  bool authoritative{true};
};

}  // namespace nicresidency
