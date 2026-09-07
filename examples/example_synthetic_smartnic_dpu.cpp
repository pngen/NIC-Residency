// example_synthetic_smartnic_dpu.cpp
//
// SmartNIC and DPU discovery: the synthetic fixture produces devices classified
// as SMARTNIC and DPU, with local-memory capability observable, and every record
// carries SYNTHETIC provenance.  The residency policy then selects each device
// for its colocated GPU.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>

#include <nicresidency/engine.hpp>
#include <nicresidency/policy.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/synthetic_backend.hpp>

using namespace nicresidency;

static int run_case(SyntheticScenario scenario, NicId nicId, GpuId gpuId,
                    DeviceKind expectedKind, const char* label) {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId boot = eng.register_host_worker(worker);

  SyntheticBackend backend(scenario);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  RegistrySnapshot snap = eng.registry().snapshot();
  const NicRecord* n = snap.nic(nicId);
  if (n == nullptr) {
    std::printf("[%s] FAIL: NIC id=%s not found\n", label, nicId.str().c_str());
    return 1;
  }

  const bool local_mem = n->capability.local_memory_observable();
  const bool synth = n->provenance.synthetic();
  std::printf("[%s] id=%s kind=%s localmem=%s prov=%s\n", label,
              n->id.str().c_str(), to_string(n->device_kind),
              local_mem ? "yes" : "no", n->provenance.str().c_str());

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);
  ResidencyDecision dec = policy.decide(snap, gpuId, NumaNodeId(), EndpointId(), worker);
  std::printf("  decision: overall=%s selected=%s\n", to_string(dec.overall),
              dec.selected.valid() ? dec.selected.str().c_str() : "NONE");

  const bool ok = (n->device_kind == expectedKind && local_mem && synth);
  std::printf("  %s: device_kind=%s local-memory observable, provenance SYNTHETIC\n",
              ok ? "PASS" : "FAIL", to_string(expectedKind));
  return ok ? 0 : 1;
}

int main() {
  int failures = 0;
  failures += run_case(SyntheticScenario::kSmartNicLocalMemory, NicId(50),
                       GpuId(104), DeviceKind::kSmartNic, "smartnic");
  failures += run_case(SyntheticScenario::kDpuLikeLocalMemory, NicId(60),
                       GpuId(105), DeviceKind::kDpu, "dpu");
  std::printf("\n%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
