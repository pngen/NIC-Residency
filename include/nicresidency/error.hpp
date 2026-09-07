#pragma once
// Typed error semantics.  Failure paths never collapse to a generic string:
// every failure carries a structured ErrorCode plus a human-readable message
// and, where relevant, an inspection detail string.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <stdexcept>
#include <string>

namespace nicresidency {

enum class ErrorCode {
  kOk = 0,
  kNotFound,
  kUnsupported,
  kInvalidArgument,
  kInvalidTopology,
  kInvalidAttachment,
  kStaleNic,
  kStaleFunction,
  kStaleQueue,
  kStaleTopology,
  kStaleResidency,
  kStaleBoot,
  kStaleEpoch,
  kCapabilityMismatch,
  kNotLocal,
  kNotReady,
  kOwnerConflict,
  kRevalidationRequired,
  kInsufficientEvidence,
  kPolicyRejected,
  kBackendUnavailable,
  kBackendError,
  kProtocolError,
  kIntegrityFailure,
  kResourceLimit,
  kCancelled,
  kInternalError,
};

inline const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk: return "OK";
    case ErrorCode::kNotFound: return "NOT_FOUND";
    case ErrorCode::kUnsupported: return "UNSUPPORTED";
    case ErrorCode::kInvalidArgument: return "INVALID_ARGUMENT";
    case ErrorCode::kInvalidTopology: return "INVALID_TOPOLOGY";
    case ErrorCode::kInvalidAttachment: return "INVALID_ATTACHMENT";
    case ErrorCode::kStaleNic: return "STALE_NIC";
    case ErrorCode::kStaleFunction: return "STALE_FUNCTION";
    case ErrorCode::kStaleQueue: return "STALE_QUEUE";
    case ErrorCode::kStaleTopology: return "STALE_TOPOLOGY";
    case ErrorCode::kStaleResidency: return "STALE_RESIDENCY";
    case ErrorCode::kStaleBoot: return "STALE_BOOT";
    case ErrorCode::kStaleEpoch: return "STALE_EPOCH";
    case ErrorCode::kCapabilityMismatch: return "CAPABILITY_MISMATCH";
    case ErrorCode::kNotLocal: return "NOT_LOCAL";
    case ErrorCode::kNotReady: return "NOT_READY";
    case ErrorCode::kOwnerConflict: return "OWNER_CONFLICT";
    case ErrorCode::kRevalidationRequired: return "REVALIDATION_REQUIRED";
    case ErrorCode::kInsufficientEvidence: return "INSUFFICIENT_EVIDENCE";
    case ErrorCode::kPolicyRejected: return "POLICY_REJECTED";
    case ErrorCode::kBackendUnavailable: return "BACKEND_UNAVAILABLE";
    case ErrorCode::kBackendError: return "BACKEND_ERROR";
    case ErrorCode::kProtocolError: return "PROTOCOL_ERROR";
    case ErrorCode::kIntegrityFailure: return "INTEGRITY_FAILURE";
    case ErrorCode::kResourceLimit: return "RESOURCE_LIMIT";
    case ErrorCode::kCancelled: return "CANCELLED";
    case ErrorCode::kInternalError: return "INTERNAL_ERROR";
  }
  return "UNKNOWN_ERROR";
}

// Thrown for typed, inspectable failures.  A caller may catch it and branch on
// the stable ErrorCode, then inspect the human-readable message / detail.
class ResidencyError : public std::runtime_error {
 public:
  explicit ResidencyError(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  ResidencyError(ErrorCode code, std::string message, std::string detail)
      : std::runtime_error(std::move(message)), code_(code),
        detail_(std::move(detail)) {}

  ErrorCode code() const noexcept { return code_; }
  const std::string& detail() const noexcept { return detail_; }

 private:
  ErrorCode code_;
  std::string detail_;
};

}  // namespace nicresidency
