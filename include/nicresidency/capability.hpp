#pragma once
// Static capability evidence, separated from dynamic state.
//
// A capability is a static feature the backend can *prove* (RDMA capable,
// SR-IOV capable, offload capable, NIC/DPU-local memory present, ...).  A
// capability is never inferred from a name, a link speed, or physical
// proximity.  Capabilities are generation-bound: when a new capability set is
// published, dependent residency must be revalidated.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>
#include <vector>

#include "nicresidency/id.hpp"
#include "nicresidency/provenance.hpp"

namespace nicresidency {

class CapabilitySet {
 public:
  CapabilitySet() = default;

  CapabilityGeneration generation() const noexcept { return generation_; }
  void set_generation(CapabilityGeneration g) noexcept { generation_ = g; }

  bool rdma_capable() const noexcept { return rdma_capable_; }
  void set_rdma_capable(bool v) noexcept { rdma_capable_ = v; }

  bool sr_iov_capable() const noexcept { return sr_iov_capable_; }
  void set_sr_iov_capable(bool v) noexcept { sr_iov_capable_ = v; }

  bool offload_capable() const noexcept { return offload_capable_; }
  void set_offload_capable(bool v) noexcept { offload_capable_ = v; }

  bool local_memory_observable() const noexcept { return local_memory_observable_; }
  void set_local_memory_observable(bool v) noexcept { local_memory_observable_ = v; }

  bool accelerator_affinity_supported() const noexcept { return accelerator_affinity_supported_; }
  void set_accelerator_affinity_supported(bool v) noexcept { accelerator_affinity_supported_ = v; }

  const Provenance& provenance() const noexcept { return provenance_; }
  void set_provenance(Provenance p) noexcept { provenance_ = std::move(p); }

  std::string str() const {
    std::string out = "gen=" + generation_.str();
    out += " rdma=" + std::string(rdma_capable_ ? "yes" : "no");
    out += " sriov=" + std::string(sr_iov_capable_ ? "yes" : "no");
    out += " offload=" + std::string(offload_capable_ ? "yes" : "no");
    out += " localmem=" + std::string(local_memory_observable_ ? "yes" : "no");
    out += " accelaffinity=" + std::string(accelerator_affinity_supported_ ? "yes" : "no");
    out += " prov=" + provenance_.str();
    return out;
  }

  bool operator==(const CapabilitySet& o) const noexcept {
    return rdma_capable_ == o.rdma_capable_ &&
           sr_iov_capable_ == o.sr_iov_capable_ &&
           offload_capable_ == o.offload_capable_ &&
           local_memory_observable_ == o.local_memory_observable_ &&
           accelerator_affinity_supported_ == o.accelerator_affinity_supported_;
  }
  bool operator!=(const CapabilitySet& o) const noexcept { return !(*this == o); }

 private:
  CapabilityGeneration generation_;
  bool rdma_capable_{false};
  bool sr_iov_capable_{false};
  bool offload_capable_{false};
  bool local_memory_observable_{false};
  bool accelerator_affinity_supported_{false};
  Provenance provenance_;
};

}  // namespace nicresidency
