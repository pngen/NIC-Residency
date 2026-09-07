#pragma once
// Vendor-neutral backend abstraction.
//
// A backend produces a DiscoveryResult containing records already labeled with
// provenance (REAL / SYNTHETIC / UNSUPPORTED).  The core never invents a
// capability the backend did not observe; UNSUPPORTED capabilities are simply
// not claimed.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nicresidency/id.hpp"
#include "nicresidency/records.hpp"
#include "nicresidency/topology.hpp"

namespace nicresidency {

enum class BackendKind {
  kSystem,
  kNvidiaAffinity,
  kSynthetic,
  kUnsupported,
};

inline const char* to_string(BackendKind k) noexcept {
  switch (k) {
    case BackendKind::kSystem: return "SYSTEM";
    case BackendKind::kNvidiaAffinity: return "NVIDIA_AFFINITY";
    case BackendKind::kSynthetic: return "SYNTHETIC";
    case BackendKind::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// Discovery context: working bounds and generation, so a backend cannot
// allocate unbounded state.
struct DiscoveryContext {
  TopologyGeneration topology_generation{TopologyGeneration(1)};
  std::size_t max_nics{256};
  std::size_t max_gpus{64};
  std::size_t max_functions{2048};
  std::size_t max_ports{2048};
  std::size_t max_queues{4096};
  std::size_t max_topology_nodes{4096};
  bool label_as_synthetic{false};  // forces SYNTHETIC only within synthetic backends
  std::uint64_t freshness{1};      // monotonic observation sequence
};

struct DiscoveryResult {
  std::vector<NicRecord> nics;
  std::vector<FunctionRecord> functions;
  std::vector<PortRecord> ports;
  std::vector<QueueRecord> queues;
  std::vector<GpuRecord> gpus;
  std::unique_ptr<TopologySnapshot> topology;  // null when not available
  std::vector<std::string> notes;              // human notes about what was / wasn't found
};

// Base class for all NIC Residency backends.
class Backend {
 public:
  virtual ~Backend() = default;
  Backend() = default;
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  virtual BackendKind kind() const noexcept = 0;
  virtual std::string name() const = 0;
  // Whether this backend produces synthetic (never REAL) evidence.
  virtual bool synthetic() const noexcept { return false; }

  // Discover the locality surface.  May throw ResidencyError(kBackendUnavailable)
  // when the underlying platform/API is not available.
  virtual DiscoveryResult discover(const DiscoveryContext& ctx) const = 0;
};

}  // namespace nicresidency
