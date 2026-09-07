// example_real_system_backend.cpp
//
// Real host discovery: use SystemBackend for REAL PCI NICs and
// NvidiaAffinityBackend (when NVML is available) for REAL accelerators.  Every
// record carries REAL provenance; nothing is simulated.  If the host has no
// discoverable NIC or GPU, a message is printed and the program still exits 0.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>

#include <nicresidency/engine.hpp>
#include <nicresidency/policy.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/system_backend.hpp>

using namespace nicresidency;

int main() {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId boot = eng.register_host_worker(worker);

  DiscoveryContext ctx;
  ctx.freshness = 1;

  try {
    SystemBackend system;
    DiscoveryResult d = system.discover(ctx);
    eng.ingest(d, worker, boot);
  } catch (const ResidencyError& err) {
    std::printf("SystemBackend: %s\n", err.what());
  }

  if (NvidiaAffinityBackend::available()) {
    try {
      NvidiaAffinityBackend nvidia;
      DiscoveryResult d = nvidia.discover(ctx);
      eng.ingest(d, worker, boot);
      std::printf("NvidiaAffinityBackend: available\n");
    } catch (const ResidencyError& err) {
      std::printf("NvidiaAffinityBackend: %s\n", err.what());
    }
  } else {
    std::printf("NvidiaAffinityBackend: UNSUPPORTED (NVML not available)\n");
  }

  const RegistrySnapshot snap = eng.registry().snapshot();

  std::printf("\nNICs (%zu):\n", snap.nic_count());
  for (const auto& kv : snap.nics()) {
    const NicRecord& n = kv.second;
    std::printf("  id=%s os=%s pci=%s prov=%s\n", n.id.str().c_str(),
                n.os_name.c_str(), n.pci.str().c_str(), n.provenance.str().c_str());
  }
  std::printf("Accelerators (%zu):\n", snap.gpu_count());
  for (const auto& kv : snap.gpus()) {
    const GpuRecord& g = kv.second;
    std::printf("  id=%s name=%s pci=%s prov=%s\n", g.id.str().c_str(),
                g.name.c_str(), g.pci.str().c_str(), g.provenance.str().c_str());
  }

  if (snap.nic_count() == 0 && snap.gpu_count() == 0) {
    std::printf("\nNothing available on this host.\n");
  }

  // Real evidence only: leave allow_synthetic false (fail closed).
  ResidencyPolicy policy;
  GpuId target_gpu;
  if (snap.gpu_count() > 0) {
    target_gpu = snap.gpus().begin()->first;
  }
  const ResidencyDecision dec =
      policy.decide(snap, target_gpu, NumaNodeId(), EndpointId(), worker);
  std::printf("\nPolicy decision: overall=%s selected=%s\n",
              to_string(dec.overall),
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
  std::printf("%s\n", snap.summary().c_str());

  return 0;
}
