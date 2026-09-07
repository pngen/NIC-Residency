
// ResidencyEngine implementation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/engine.hpp"

#include <chrono>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nicresidency {

WorkerBootId fresh_boot_id() {
  std::uint64_t pidPart = 0;
#ifdef _WIN32
  pidPart = static_cast<std::uint64_t>(::GetCurrentProcessId());
#endif
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  std::random_device rd;
  std::uint64_t randPart = (static_cast<std::uint64_t>(rd()) << 32) ^
                           static_cast<std::uint64_t>(rd());
  std::uint64_t value = pidPart ^ (static_cast<std::uint64_t>(now) & 0x7FFFFFFFFFFFFFFFULL);
  value ^= randPart;
  if (value == 0) value = 1;
  return WorkerBootId(value);
}

void ResidencyEngine::ingest(const DiscoveryResult& d, WorkerId worker, WorkerBootId boot,
                             CoordinatorEpoch epoch) {
  for (const auto& nic : d.nics) {
    registry_.register_nic(nic, epoch, worker, boot);
  }
  for (const auto& gpu : d.gpus) {
    registry_.register_gpu(gpu, epoch, worker, boot);
  }
  if (d.topology != nullptr) {
    registry_.set_topology(*d.topology, epoch, worker, boot);
  }
  for (const auto& func : d.functions) {
    registry_.register_function(func, epoch, worker, boot);
  }
  for (const auto& port : d.ports) {
    registry_.register_port(port, epoch, worker, boot);
  }
  for (const auto& q : d.queues) {
    registry_.register_queue(q, epoch, worker, boot);
  }
}

}  // namespace nicresidency
