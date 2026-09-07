// example_unsupported_smartnic.cpp
//
// Unsupported backend: UnsupportedBackend deliberately claims nothing.  No NIC
// or SmartNIC capability is discovered, so the policy correctly reports
// NOT_FOUND rather than over-claiming a SmartNIC the platform cannot prove.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>

#include <nicresidency/engine.hpp>
#include <nicresidency/policy.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/unsupported_backend.hpp>

using namespace nicresidency;

int main() {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId boot = eng.register_host_worker(worker);

  UnsupportedBackend backend;
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  std::printf("Backend kind: %s (name=%s)\n", to_string(backend.kind()),
              backend.name().c_str());
  for (const auto& note : d.notes) {
    std::printf("  note: %s\n", note.c_str());
  }

  const RegistrySnapshot snap = eng.registry().snapshot();
  std::printf("NICs discovered: %zu\n", snap.nic_count());
  std::printf("  no SmartNIC, RDMA, SR-IOV, offload, or local-memory capability "
              "is claimed by this backend.\n");

  ResidencyPolicy policy;
  const ResidencyDecision dec =
      policy.decide(snap, GpuId(), NumaNodeId(), EndpointId(), worker);
  std::printf("\nPolicy decision: overall=%s selected=%s\n",
              to_string(dec.overall),
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
  std::printf("  => no SmartNIC was invented; the capability set is UNSUPPORTED.\n");

  return 0;
}
