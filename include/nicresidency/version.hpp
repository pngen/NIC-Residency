#pragma once
// NIC Residency - vendor-neutral runtime for NIC / SmartNIC / DPU resource
// locality, residency, and authority in accelerator infrastructure.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>

namespace nicresidency {

inline constexpr int kVersionMajor = 1;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;

inline constexpr const char* kVersionString = "1.0.0";
inline constexpr const char* kProductName = "NIC Residency";
inline constexpr const char* kVendor = "Summon Software Labs";
inline constexpr std::uint32_t kLibraryApiVersion = 6;

// Persistence / protocol format versions.  Bumped whenever the durable,
// on-disk or on-the-wire representation changes incompatibly.
inline constexpr std::uint32_t kPersistenceFormatVersion = 1;
inline constexpr std::uint32_t kProtocolVersion = 1;

}  // namespace nicresidency
