#pragma once
// ResidencyEngine: a convenience composition layer tying a backend discovery
// result to the registry under a real process authority (host worker).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>

#include "nicresidency/backend.hpp"
#include "nicresidency/registry.hpp"

namespace nicresidency {

// A fresh WorkerBootId for the current process incarnation.  Uniqueness comes
// from a high-resolution clock plus an entropy source; it is never a
// process-local adapter/queue handle.
WorkerBootId fresh_boot_id();

class ResidencyEngine {
 public:
  explicit ResidencyEngine(CoordinatorEpoch epoch) : registry_(epoch) {}

  NicRegistry& registry() noexcept { return registry_; }
  const NicRegistry& registry() const noexcept { return registry_; }
  CoordinatorEpoch epoch() const noexcept { return registry_.epoch(); }

  // Register the host (this process) as a worker authority with a fresh boot id.
  WorkerBootId register_host_worker(WorkerId worker) {
    WorkerBootId boot = fresh_boot_id();
    registry_.register_worker(worker, boot);
    return boot;
  }

  // Ingest backend discovery under the supplied host authority.  Only durable
  // identity is registered; dynamic readiness fields are left to revalidation.
  void ingest(const DiscoveryResult& d, WorkerId worker, WorkerBootId boot,
              CoordinatorEpoch epoch);

  // Convenience wrapper using the engine epoch.
  void ingest(const DiscoveryResult& d, WorkerId worker, WorkerBootId boot) {
    ingest(d, worker, boot, registry_.epoch());
  }

 private:
  NicRegistry registry_;
};

}  // namespace nicresidency
