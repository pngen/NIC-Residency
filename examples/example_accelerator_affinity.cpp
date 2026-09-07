// example_accelerator_affinity.cpp
//
// Accelerator affinity: two synthetic NICs share the same root complex but sit
// behind different PCIe switches, plus a GPU behind the same switch as one of
// them.  The same-switch NIC must rank above the same-root-complex-only NIC.
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

  SyntheticBackend backend(SyntheticScenario::kSamePcieSwitchPreference);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  RegistrySnapshot snap = eng.registry().snapshot();

  std::printf("Candidate NICs: (same root complex, different PCIe switches)\n");
  for (const auto& kv : snap.nics()) {
    const NicRecord& n = kv.second;
    std::printf("  id=%s pci=%s numa=%llu\n", n.id.str().c_str(),
                n.pci.str().c_str(), (unsigned long long)n.numa_node.value());
  }

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);
  ResidencyDecision dec =
      policy.decide(snap, GpuId(102), NumaNodeId(), EndpointId(), worker);

  std::printf("\nTarget GPU id=%s (behind the same switch as the NUMA-0 NIC)\n",
              GpuId(102).str().c_str());
  std::printf("Overall=%s\n", to_string(dec.overall));
  std::printf("Selected NIC=%s\n",
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
  for (const auto& c : dec.evaluated) {
    std::printf("  candidate=%s elig=%s score=%lld\n", c.nic.str().c_str(),
                to_string(c.eligibility), (long long)c.rank_score);
  }

  // Same-switch NIC (id=30) must outrank same-root-complex-only (id=31).
  const bool same_switch_wins = (dec.selected == NicId(30));
  std::printf("\n%s: expected same-switch NIC id=%s to win\n",
              same_switch_wins ? "PASS" : "FAIL", NicId(30).str().c_str());

  return same_switch_wins ? 0 : 1;
}