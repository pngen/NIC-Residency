#pragma once
// SystemBackend: REAL host discovery using operating-system APIs.
//
// On Windows this discovers network adapters (SetupAPI / NetAdapter), their
// PCI identity and NUMA node, builds PCI hierarchy evidence, and pairs with an
// optional NVIDIA accelerator-affinity helper.  Everything untouched is left
// UNSUPPORTED; nothing is simulated as real.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <memory>

#include "nicresidency/backend.hpp"

namespace nicresidency {

class SystemBackend : public Backend {
 public:
  SystemBackend() = default;
  ~SystemBackend() override = default;

  BackendKind kind() const noexcept override { return BackendKind::kSystem; }
  std::string name() const override { return "windows-system"; }
  bool synthetic() const noexcept override { return false; }

  DiscoveryResult discover(const DiscoveryContext& ctx) const override;

 private:
  std::unique_ptr<TopologySnapshot> build_topology(TopologyGeneration gen,
                                                   std::size_t max_nodes) const;
};

// A thin helper for accelerator affinity that can run independently.
class NvidiaAffinityBackend : public Backend {
 public:
  NvidiaAffinityBackend() = default;
  ~NvidiaAffinityBackend() override = default;

  BackendKind kind() const noexcept override { return BackendKind::kNvidiaAffinity; }
  std::string name() const override { return "nvidia-affinity"; }
  bool synthetic() const noexcept override { return false; }

  DiscoveryResult discover(const DiscoveryContext& ctx) const override;

  // Returns true when NVML / CUDA runtime is actually present and usable.
  static bool available() noexcept;
};

}  // namespace nicresidency
