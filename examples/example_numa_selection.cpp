// example_numa_selection.cpp
//
// NUMA-aware selection: two synthetic NICs on different NUMA nodes, and a GPU
// on the first NUMA node.  The policy must rank the same-NUMA NIC above the
// remote-NUMA NIC.
//
// Note: the synthetic fixture labels the first NUMA node with the null id
// (NumaNodeId(0) is the invalid id), so we re-encode the NICs and the GPU with
// valid, distinct NUMA ids before deciding.  This exercises the same-NUMA
// ranking path rather than accidentally relying on id tie-breaking.
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

  SyntheticBackend backend(SyntheticScenario::kTwoNicsDifferentNuma);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  // Re-encode valid NUMA ids: the GPU and the "NUMA-0" NIC on node 1, the
  // remote NIC on node 2.  (NumaNodeId(0) is the invalid/null id.)
  NicRecord nic0 = *eng.registry().snapshot().nic(NicId(20));
  nic0.generation = NicGeneration(2);
  nic0.numa_node = NumaNodeId(1);
  eng.registry().register_nic(nic0, eng.epoch(), worker, boot);

  NicRecord nic1 = *eng.registry().snapshot().nic(NicId(21));
  nic1.generation = NicGeneration(2);
  nic1.numa_node = NumaNodeId(2);
  eng.registry().register_nic(nic1, eng.epoch(), worker, boot);

  GpuRecord gpu0 = *eng.registry().snapshot().gpu(GpuId(101));
  gpu0.numa_node = NumaNodeId(1);
  eng.registry().register_gpu(gpu0, eng.epoch(), worker, boot);

  RegistrySnapshot snap = eng.registry().snapshot();

  std::printf("Candidate NICs (different NUMA nodes):\n");
  for (const auto& kv : snap.nics()) {
    const NicRecord& n = kv.second;
    std::printf("  id=%s numa=%llu pci=%s\n", n.id.str().c_str(),
                (unsigned long long)n.numa_node.value(), n.pci.str().c_str());
  }

  // The GPU on the first (same) NUMA node.
  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);
  ResidencyDecision dec =
      policy.decide(snap, GpuId(101), NumaNodeId(), EndpointId(), worker);

  std::printf("\nTarget GPU id=%s on the same NUMA node\n",
              GpuId(101).str().c_str());
  std::printf("Overall=%s\n", to_string(dec.overall));
  std::printf("Selected NIC=%s\n",
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
  for (const auto& c : dec.evaluated) {
    std::printf("  candidate=%s elig=%s score=%lld\n", c.nic.str().c_str(),
                to_string(c.eligibility), (long long)c.rank_score);
  }

  const bool ok = (dec.selected == NicId(20));
  std::printf("\n%s: expected the same-NUMA NIC id=%s\n",
              ok ? "PASS" : "FAIL", NicId(20).str().c_str());

  return ok ? 0 : 1;
}
