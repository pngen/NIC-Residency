#pragma once
// Core domain records.  Strongly typed identities only; no raw integer / string
// identity interop, and no persistence of process-local object handles.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <string>

#include "nicresidency/capability.hpp"
#include "nicresidency/enums.hpp"
#include "nicresidency/id.hpp"
#include "nicresidency/locality.hpp"
#include "nicresidency/provenance.hpp"

namespace nicresidency {

// ---------------------------------------------------------------------------
// NicRecord: one stable NIC / SmartNIC / DPU-facing device.
// ---------------------------------------------------------------------------
struct NicRecord {
  NicId id;
  NicGeneration generation;

  std::string os_name;        // stable OS/backend identity (name/LUID), never a handle
  std::string os_description; // human description from the backend

  PciAddress pci;             // stable PCI identity
  std::uint16_t vendor_id{0};
  std::uint16_t device_id{0};
  std::uint16_t subsystem_vendor_id{0};
  std::uint16_t subsystem_id{0};
  std::uint8_t revision{0};

  DeviceKind device_kind{DeviceKind::kNic};
  FunctionClass function_class{FunctionClass::kUnknown};

  CapabilitySet capability;
  NumaNodeId numa_node;
  LinkState link_state{LinkState::kUnknown};
  HealthState health{HealthState::kUnknown};

  LifecycleState lifecycle{LifecycleState::kDiscovered};
  Provenance provenance;

  std::uint64_t freshness{0};  // monotonic observation sequence

  bool valid() const noexcept { return id.valid(); }
};

// ---------------------------------------------------------------------------
// FunctionRecord: PCI / SR-IOV function identity, only where observable.
// ---------------------------------------------------------------------------
struct FunctionRecord {
  FunctionId id;
  FunctionGeneration generation;
  NicId parent_nic;
  FunctionClass function_class{FunctionClass::kUnknown};

  PortId port;  // optional single-port association
  NumaNodeId numa_node;
  CapabilitySet capability;
  OwnershipState ownership{OwnershipState::kUnknown};
  WorkerId owner_worker;
  WorkerBootId owner_boot;
  CoordinatorEpoch epoch;

  LifecycleState lifecycle{LifecycleState::kDiscovered};
  Provenance provenance;
  std::uint64_t freshness{0};

  bool valid() const noexcept { return id.valid() && parent_nic.valid(); }
};

// ---------------------------------------------------------------------------
// PortRecord: an observable NIC port.
// ---------------------------------------------------------------------------
struct PortRecord {
  PortId id;
  PortGeneration generation;
  NicId parent_nic;
  FunctionId parent_function;  // optional
  std::string os_name;
  LinkState link_state{LinkState::kUnknown};
  std::uint64_t link_speed_mbps{0};  // only when observed
  NumaNodeId numa_node;
  LifecycleState lifecycle{LifecycleState::kDiscovered};
  Provenance provenance;
  std::uint64_t freshness{0};

  bool valid() const noexcept { return id.valid() && parent_nic.valid(); }
};

// ---------------------------------------------------------------------------
// QueueRecord: observable queue/resource residency.
// ---------------------------------------------------------------------------
struct QueueRecord {
  QueueId id;
  QueueGeneration generation;
  FunctionId parent_function;
  NicId parent_nic;
  std::string kind;  // e.g. "RX", "TX", "COMPLETION", "QUEUE_GROUP", "RDMA"

  LifecycleState lifecycle{LifecycleState::kDiscovered};
  Provenance provenance;
  std::uint64_t freshness{0};

  bool valid() const noexcept { return id.valid() && parent_function.valid(); }
};

// ---------------------------------------------------------------------------
// GpuRecord: an accelerator, used as a residency target.
// ---------------------------------------------------------------------------
struct GpuRecord {
  GpuId id;
  GpuGeneration generation;
  std::string name;
  PciAddress pci;
  DeviceId cuda_device;  // CUDA device ordinal when CUDA verified (stable backend ordinal)
  bool cuda_verified{false};
  NumaNodeId numa_node;
  Provenance provenance;
  std::uint64_t freshness{0};

  bool valid() const noexcept { return id.valid(); }
};

// ---------------------------------------------------------------------------
// Attachment: a governed relationship between two resources.
// ---------------------------------------------------------------------------
struct Attachment {
  AttachmentId id;
  AttachmentGeneration generation;

  NicId nic;
  FunctionId function;  // optional
  PortId port;          // optional
  QueueId queue;        // optional

  GpuId gpu;            // optional endpoint
  NumaNodeId numa;      // optional endpoint
  EndpointId endpoint;  // optional endpoint

  TopologyGeneration topology_generation;
  AttachmentState state{AttachmentState::kProposed};

  WorkerId owner_worker;
  WorkerBootId owner_boot;
  CoordinatorEpoch epoch;

  Provenance provenance;
  std::uint64_t freshness{0};

  bool valid() const noexcept { return id.valid(); }
};

// ---------------------------------------------------------------------------
// ResidencyRecord: the first-class residency binding of a NIC resource set to
// a target compute/memory/accelerator domain.
// ---------------------------------------------------------------------------
struct ResidencyRecord {
  ResidencyId id;
  ResidencyGeneration generation;

  NicId nic;
  FunctionId function;  // optional
  PortId port;          // optional
  QueueId queue;        // optional

  GpuId target_gpu;     // context (optional)
  NumaNodeId target_numa;
  EndpointId target_endpoint;  // optional

  ResidencyState state{ResidencyState::kDiscovered};
  AttachmentGeneration attachment_generation;
  TopologyGeneration topology_generation;
  CapabilityGeneration capability_generation;

  OwnershipState ownership{OwnershipState::kUnknown};
  WorkerId owner_worker;
  WorkerBootId owner_boot;
  CoordinatorEpoch epoch;

  Locality locality;
  PolicyId policy;
  Provenance provenance;
  std::uint64_t freshness{0};
  LifecycleState lifecycle{LifecycleState::kDiscovered};
  std::string explanation;  // structured, human-readable explanation

  bool valid() const noexcept { return id.valid() && nic.valid(); }
};

}  // namespace nicresidency
