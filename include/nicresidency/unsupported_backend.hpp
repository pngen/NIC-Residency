#pragma once
// UnsupportedBackend: an explicit no-op backend that reports every capability
// as UNSUPPORTED.  Used when a real/synthetic backend would overclaim.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/backend.hpp"

namespace nicresidency {

class UnsupportedBackend : public Backend {
 public:
  UnsupportedBackend() = default;
  ~UnsupportedBackend() override = default;

  BackendKind kind() const noexcept override { return BackendKind::kUnsupported; }
  std::string name() const override { return "unsupported"; }
  bool synthetic() const noexcept override { return false; }

  DiscoveryResult discover(const DiscoveryContext& ctx) const override {
    DiscoveryResult out;
    out.notes.push_back("unsupported backend: no capabilities claimed");
    (void)ctx;
    return out;
  }
};

}  // namespace nicresidency
