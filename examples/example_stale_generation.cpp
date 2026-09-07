// example_stale_generation.cpp
//
// Stale device evidence: a NIC is discovered and a decision is made.  The NIC
// generation is then advanced (within the host worker's authority) and the
// device must be revalidated before it can be used again, so the next decision
// reports REVALIDATION_REQUIRED.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>

#include <nicresidency/engine.hpp>
#include <nicresidency/policy.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/synthetic_backend.hpp>

using namespace nicresidency;

int main() {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId boot = eng.register_host_worker(worker);

  SyntheticBackend backend(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);

  RegistrySnapshot snap = eng.registry().snapshot();
  const NicRecord* first = snap.nic(NicId(10));
  if (first == nullptr) {
    std::printf("FAIL: NIC id=%s not found\n", NicId(10).str().c_str());
    return 1;
  }
  std::printf("NIC id=%s generation=%s\n", first->id.str().c_str(),
              first->generation.str().c_str());

  ResidencyDecision before =
      policy.decide(snap, GpuId(100), NumaNodeId(), EndpointId(), worker);
  std::printf("\nDecision BEFORE generation advance:\n");
  std::printf("  overall=%s selected=%s\n", to_string(before.overall),
              before.selected.valid() ? before.selected.str().c_str() : "NONE");

  // Advance the NIC generation under the host worker's authority.
  eng.registry().advance_nic_generation(NicId(10), eng.epoch(), worker, boot);
  std::printf("\nAdvanced NIC generation -> new generation:\n");
  const RegistrySnapshot afterAdvance = eng.registry().snapshot();
  const NicRecord* advanced = afterAdvance.nic(NicId(10));
  if (advanced != nullptr) {
    std::printf("  NIC id=%s generation=%s\n", advanced->id.str().c_str(),
                advanced->generation.str().c_str());
  }

  // The generation bump means the previously published evidence is no longer
  // authoritative: model the device as requiring revalidation before a fresh
  // decision.  (Re-publishing at a higher generation replaces the old record.)
  NicRecord stale = *advanced;
  stale.generation = NicGeneration(3);
  stale.lifecycle = LifecycleState::kRevalidationRequired;
  eng.registry().register_nic(stale, eng.epoch(), worker, boot);

  const RegistrySnapshot after = eng.registry().snapshot();
  ResidencyDecision after_dec =
      policy.decide(after, GpuId(100), NumaNodeId(), EndpointId(), worker);
  std::printf("\nDecision AFTER generation advance (device requires revalidation):\n");
  std::printf("  overall=%s selected=%s\n", to_string(after_dec.overall),
              after_dec.selected.valid() ? after_dec.selected.str().c_str() : "NONE");
  for (const auto& c : after_dec.evaluated) {
    std::printf("  candidate=%s elig=%s\n", c.nic.str().c_str(),
                to_string(c.eligibility));
    for (const auto& reason : c.rejections) {
      std::printf("      reject: %s  (%s)\n", reason.name.c_str(), reason.detail.c_str());
    }
  }

  const bool ok = (after_dec.overall == ResidencyEligibility::kRevalidationRequired);
  std::printf("\n%s: the advanced device is reported REVALIDATION_REQUIRED\n",
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
