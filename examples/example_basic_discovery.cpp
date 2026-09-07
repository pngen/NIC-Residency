// example_basic_discovery.cpp
//
// Basic discovery: build a registry from a synthetic one-NIC/one-GPU scenario,
// list the discovered NICs, and run the residency policy to pick the best NIC
// for the GPU.
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
  // One process authority for the host worker.
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId boot = eng.register_host_worker(worker);

  SyntheticBackend backend(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  RegistrySnapshot snap = eng.registry().snapshot();

  std::printf("Discovered NICs (%zu):\n", snap.nic_count());
  for (const auto& kv : snap.nics()) {
    const NicRecord& n = kv.second;
    std::printf("  id=%s gen=%llu os=%s pci=%s kind=%s numa=%llu link=%s prov=%s\n",
                n.id.str().c_str(), (unsigned long long)n.generation.value(),
                n.os_name.c_str(), n.pci.str().c_str(), to_string(n.device_kind),
                (unsigned long long)n.numa_node.value(), to_string(n.link_state),
                n.provenance.str().c_str());
  }

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);  // synthetic evidence is admissible here
  policy.set_policy_id(PolicyId(7));

  ResidencyDecision dec =
      policy.decide(snap, GpuId(100), NumaNodeId(), EndpointId(), worker);

  std::printf("\nOverall=%s\n", to_string(dec.overall));
  std::printf("Selected NIC=%s\n",
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
  std::printf("Explanation: %s\n", dec.explanation.c_str());

  return 0;
}
