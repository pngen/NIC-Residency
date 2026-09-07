// bench_selection.cpp
// NIC Residency benchmark: ResidencyPolicy::decide(...) over the SYNTHETIC
// dense candidate set (kDenseCandidateSet -> 200 NICs).  Every NIC is clearly
// labeled SYNTHETIC; this measures policy evaluation cost on a synthetic
// fixture, never hardware residency performance.
//
// SPDX-License-Identifier: Apache-2.0
#include <nicresidency/engine.hpp>
#include <nicresidency/policy.hpp>
#include <nicresidency/records.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/residency.hpp>
#include <nicresidency/synthetic_backend.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace nicresidency;

int main() {
  constexpr std::size_t kIters = 2000;

  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);

  SyntheticBackend sb(SyntheticScenario::kDenseCandidateSet);
  DiscoveryContext ctx;
  ctx.label_as_synthetic = true;
  ctx.freshness = 1;
  DiscoveryResult d = sb.discover(ctx);
  eng.ingest(d, w, boot);

  RegistrySnapshot snap = eng.registry().snapshot();
  const std::size_t nic_count = snap.nic_count();
  const std::size_t topo_nodes = snap.topology() ? snap.topology()->node_count() : 0;

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);   // synthetic fixtures are eligible here
  policy.set_policy_id(PolicyId(7));
  // kDenseCandidateSet places a single GPU (GPU 107) on NUMA node 0.
  const GpuId target_gpu(107);
  const NumaNodeId target_numa;      // invalid -> derived from topology
  const EndpointId endpoint;
  const WorkerId workload(1);

  auto t0 = std::chrono::steady_clock::now();
  std::size_t allowed_total = 0;
  std::uint32_t selected_ok = 0;
  for (std::size_t i = 0; i < kIters; ++i) {
    ResidencyDecision dec =
        policy.decide(snap, target_gpu, target_numa, endpoint, workload);
    allowed_total += dec.candidates.size();
    if (dec.selected.valid()) ++selected_ok;
  }
  auto t1 = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double ns = ms * 1e6 / static_cast<double>(kIters);

  std::printf(
      "bench_selection: %zu iterations in %.3f ms (%.1f ns/op), "
      "context: nics=%zu funcs=0 queues=0 attachments=0 candidates=%zu "
      "topo_nodes=%zu (synthetic) avg_allowed/call=%zu\n",
      kIters, ms, ns, nic_count, nic_count, topo_nodes,
      allowed_total / static_cast<std::size_t>(kIters));
  std::printf("bench_selection_selected: %u/%zu calls produced a selected NIC\n",
              selected_ok, kIters);
  return 0;
}
