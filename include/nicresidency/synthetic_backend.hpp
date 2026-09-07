#pragma once
// SyntheticBackend: a rigorous, deterministic fixture backend for scenarios
// that cannot be exercised on the current physical machine.  Every record it
// produces is clearly labeled SYNTHETIC and never becomes REAL after reload.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/backend.hpp"

namespace nicresidency {

enum class SyntheticScenario {
  kOneNicLocalToOneGpu,
  kTwoNicsDifferentNuma,
  kSameRootComplexPreference,
  kSamePcieSwitchPreference,
  kRemoteNumaFallback,
  kSmartNicLocalMemory,
  kDpuLikeLocalMemory,
  kPfWithMultipleVfs,
  kDenseCandidateSet,
  kUnsupportedCapability,
};

inline const char* to_string(SyntheticScenario s) noexcept {
  switch (s) {
    case SyntheticScenario::kOneNicLocalToOneGpu: return "ONE_NIC_LOCAL_TO_ONE_GPU";
    case SyntheticScenario::kTwoNicsDifferentNuma: return "TWO_NICS_DIFFERENT_NUMA";
    case SyntheticScenario::kSameRootComplexPreference: return "SAME_ROOT_COMPLEX_PREFERENCE";
    case SyntheticScenario::kSamePcieSwitchPreference: return "SAME_PCIE_SWITCH_PREFERENCE";
    case SyntheticScenario::kRemoteNumaFallback: return "REMOTE_NUMA_FALLBACK";
    case SyntheticScenario::kSmartNicLocalMemory: return "SMARTNIC_LOCAL_MEMORY";
    case SyntheticScenario::kDpuLikeLocalMemory: return "DPU_LIKE_LOCAL_MEMORY";
    case SyntheticScenario::kPfWithMultipleVfs: return "PF_WITH_MULTIPLE_VFS";
    case SyntheticScenario::kDenseCandidateSet: return "DENSE_CANDIDATE_SET";
    case SyntheticScenario::kUnsupportedCapability: return "UNSUPPORTED_CAPABILITY";
  }
  return "UNKNOWN";
}

// Builds a canonical synthetic scenario into a DiscoveryResult.  Deterministic:
// the same scenario always yields the same identities / topology.
class SyntheticBackend : public Backend {
 public:
  SyntheticBackend();
  explicit SyntheticBackend(SyntheticScenario scenario);
  ~SyntheticBackend() override = default;

  BackendKind kind() const noexcept override { return BackendKind::kSynthetic; }
  std::string name() const override { return "synthetic"; }
  bool synthetic() const noexcept override { return true; }

  void set_scenario(SyntheticScenario scenario) noexcept { scenario_ = scenario; }
  SyntheticScenario scenario() const noexcept { return scenario_; }

  DiscoveryResult discover(const DiscoveryContext& ctx) const override;

 private:
  SyntheticScenario scenario_{SyntheticScenario::kOneNicLocalToOneGpu};
};

}  // namespace nicresidency
