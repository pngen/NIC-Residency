// example_deterministic_tiebreak.cpp
//
// Deterministic tie-breaking: a dense candidate set must yield the SAME selected
// NIC regardless of the order in which the set was registered.  We register the
// identical candidate set into two independent registries -- one in the
// backend's natural order, one in reverse -- and assert the same NIC wins.
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
  // Two independent engines/registries, each with its own worker authority.
  ResidencyEngine engA(CoordinatorEpoch(1));
  WorkerId workerA(1);
  WorkerBootId bootA = engA.register_host_worker(workerA);

  ResidencyEngine engB(CoordinatorEpoch(1));
  WorkerId workerB(1);
  WorkerBootId bootB = engB.register_host_worker(workerB);

  SyntheticBackend backend(SyntheticScenario::kDenseCandidateSet);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);

  // Registry A: ingest the whole result in the backend's natural order.
  engA.ingest(d, workerA, bootA);

  // Registry B: register the SAME set, but insert the NICs in reverse order to
  // prove the outcome is insertion-order independent.
  NicRegistry& regB = engB.registry();
  for (auto it = d.nics.rbegin(); it != d.nics.rend(); ++it) {
    regB.register_nic(*it, engB.epoch(), workerB, bootB);
  }
  for (const auto& g : d.gpus) {
    regB.register_gpu(g, engB.epoch(), workerB, bootB);
  }
  if (d.topology != nullptr) {
    regB.set_topology(*d.topology, engB.epoch(), workerB, bootB);
  }

  const RegistrySnapshot snapA = engA.registry().snapshot();
  const RegistrySnapshot snapB = engB.registry().snapshot();

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);  // synthetic evidence admitted for this fixture

  const ResidencyDecision decA =
      policy.decide(snapA, GpuId(107), NumaNodeId(), EndpointId(), workerA);
  const ResidencyDecision decB =
      policy.decide(snapB, GpuId(107), NumaNodeId(), EndpointId(), workerB);

  int failures = 0;

  if (!decA.selected.valid()) {
    std::printf("FAIL: registry A selected no NIC\n");
    ++failures;
  }
  if (!decB.selected.valid()) {
    std::printf("FAIL: registry B selected no NIC\n");
    ++failures;
  }
  if (decA.selected != decB.selected) {
    std::printf("FAIL: selection depends on insertion order\n");
    ++failures;
  }
  if (decA.selected != NicId(200)) {
    std::printf("FAIL: expected the deterministic first candidate id=%s\n",
                NicId(200).str().c_str());
    ++failures;
  }

  std::printf("Registry A: candidates=%zu selected=%s\n", decA.candidates.size(),
              decA.selected.valid() ? decA.selected.str().c_str() : "NONE");
  std::printf("Registry B: candidates=%zu selected=%s\n", decB.candidates.size(),
              decB.selected.valid() ? decB.selected.str().c_str() : "NONE");
  std::printf("\n%s: the same NIC was selected in both insertion orders\n",
              failures == 0 ? "PASS" : "FAIL");

  return failures == 0 ? 0 : 1;
}
