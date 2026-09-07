#pragma once
// Core enumerated domain vocabulary for NIC Residency.
//
// These enums encode the distinction that "present" != "resident",
// "resident" != "eligible", "attached" != "healthy", and "local" !=
// "authoritative".
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>

namespace nicresidency {

// ---------------------------------------------------------------------------
// ResidencyState describes whether a NIC/function/port/queue resource is
// currently resident at a target compute/memory domain.
// ---------------------------------------------------------------------------
enum class ResidencyState {
  kAbsent,
  kDiscovered,
  kAvailable,
  kResident,
  kAttached,
  kDetached,
  kDraining,
  kRevalidationRequired,
  kStale,
  kFailed,
  kUnsupported,
  kRetired,
};

inline const char* to_string(ResidencyState s) noexcept {
  switch (s) {
    case ResidencyState::kAbsent: return "ABSENT";
    case ResidencyState::kDiscovered: return "DISCOVERED";
    case ResidencyState::kAvailable: return "AVAILABLE";
    case ResidencyState::kResident: return "RESIDENT";
    case ResidencyState::kAttached: return "ATTACHED";
    case ResidencyState::kDetached: return "DETACHED";
    case ResidencyState::kDraining: return "DRAINING";
    case ResidencyState::kRevalidationRequired: return "REVALIDATION_REQUIRED";
    case ResidencyState::kStale: return "STALE";
    case ResidencyState::kFailed: return "FAILED";
    case ResidencyState::kUnsupported: return "UNSUPPORTED";
    case ResidencyState::kRetired: return "RETIRED";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// LifecycleState is the guarded lifecycle of a single residency-authoritative
// object (NIC, function, port, queue, attachment).
// ---------------------------------------------------------------------------
enum class LifecycleState {
  kDiscovered,
  kAvailable,
  kReservedForAttachment,
  kAttached,
  kResident,
  kDraining,
  kDetached,
  kRevalidationRequired,
  kFailed,
  kRetired,
};

inline const char* to_string(LifecycleState s) noexcept {
  switch (s) {
    case LifecycleState::kDiscovered: return "DISCOVERED";
    case LifecycleState::kAvailable: return "AVAILABLE";
    case LifecycleState::kReservedForAttachment: return "RESERVED_FOR_ATTACHMENT";
    case LifecycleState::kAttached: return "ATTACHED";
    case LifecycleState::kResident: return "RESIDENT";
    case LifecycleState::kDraining: return "DRAINING";
    case LifecycleState::kDetached: return "DETACHED";
    case LifecycleState::kRevalidationRequired: return "REVALIDATION_REQUIRED";
    case LifecycleState::kFailed: return "FAILED";
    case LifecycleState::kRetired: return "RETIRED";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// FunctionClass classifies the PCI / SR-IOV function role.  A function is only
// classified to a class the backend can actually prove.
// ---------------------------------------------------------------------------
enum class FunctionClass {
  kPhysicalFunction,
  kVirtualFunction,
  kSubfunction,
  kRepresentor,
  kUnknown,
};

inline const char* to_string(FunctionClass c) noexcept {
  switch (c) {
    case FunctionClass::kPhysicalFunction: return "PHYSICAL_FUNCTION";
    case FunctionClass::kVirtualFunction: return "VIRTUAL_FUNCTION";
    case FunctionClass::kSubfunction: return "SUBFUNCTION";
    case FunctionClass::kRepresentor: return "REPRESENTOR";
    case FunctionClass::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// DeviceKind classifies whether the device is a plain NIC, a SmartNIC, or a
// DPU.  Classification is evidence-based and never inferred from a name.
// ---------------------------------------------------------------------------
enum class DeviceKind {
  kNic,
  kSmartNic,
  kDpu,
  kUnknown,
  kUnsupported,
};

inline const char* to_string(DeviceKind k) noexcept {
  switch (k) {
    case DeviceKind::kNic: return "NIC";
    case DeviceKind::kSmartNic: return "SMARTNIC";
    case DeviceKind::kDpu: return "DPU";
    case DeviceKind::kUnknown: return "UNKNOWN";
    case DeviceKind::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// OwnershipState describes who currently holds authority over a resource.
// ---------------------------------------------------------------------------
enum class OwnershipState {
  kUnassigned,
  kAssigned,
  kContested,
  kRevoked,
  kUnknown,
};

inline const char* to_string(OwnershipState o) noexcept {
  switch (o) {
    case OwnershipState::kUnassigned: return "UNASSIGNED";
    case OwnershipState::kAssigned: return "ASSIGNED";
    case OwnershipState::kContested: return "CONTESTED";
    case OwnershipState::kRevoked: return "REVOKED";
    case OwnershipState::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// AttachmentState defines the guarded attachment lifecycle between two
// governed resources (e.g. NIC <-> NUMA node, function <-> worker owner).
// ---------------------------------------------------------------------------
enum class AttachmentState {
  kProposed,
  kValidated,
  kCommitted,
  kDraining,
  kRevoked,
  kStale,
  kFailed,
};

inline const char* to_string(AttachmentState a) noexcept {
  switch (a) {
    case AttachmentState::kProposed: return "PROPOSED";
    case AttachmentState::kValidated: return "VALIDATED";
    case AttachmentState::kCommitted: return "COMMITTED";
    case AttachmentState::kDraining: return "DRAINING";
    case AttachmentState::kRevoked: return "REVOKED";
    case AttachmentState::kStale: return "STALE";
    case AttachmentState::kFailed: return "FAILED";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// LinkState is the observed NIC link status.  UNSUPPORTED is used when the
// backend genuinely cannot observe link state for a resource.
// ---------------------------------------------------------------------------
enum class LinkState {
  kUp,
  kDown,
  kUnknown,
  kUnsupported,
};

inline const char* to_string(LinkState s) noexcept {
  switch (s) {
    case LinkState::kUp: return "UP";
    case LinkState::kDown: return "DOWN";
    case LinkState::kUnknown: return "UNKNOWN";
    case LinkState::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// HealthState covers the readiness/health evidence required for residency
// eligibility, and nothing more.
// ---------------------------------------------------------------------------
enum class HealthState {
  kReady,
  kDegraded,
  kNotReady,
  kUnknown,
  kUnsupported,
};

inline const char* to_string(HealthState s) noexcept {
  switch (s) {
    case HealthState::kReady: return "READY";
    case HealthState::kDegraded: return "DEGRADED";
    case HealthState::kNotReady: return "NOT_READY";
    case HealthState::kUnknown: return "UNKNOWN";
    case HealthState::kUnsupported: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

}  // namespace nicresidency
