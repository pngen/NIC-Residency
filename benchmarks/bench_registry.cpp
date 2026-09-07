// bench_registry.cpp
// NIC Residency benchmark: NIC registration throughput and RegistrySnapshot
// snapshot cost over a dense, synthetic register-only workload.
//
// SPDX-License-Identifier: Apache-2.0
#include <nicresidency/capability.hpp>
#include <nicresidency/enums.hpp>
#include <nicresidency/engine.hpp>
#include <nicresidency/locality.hpp>
#include <nicresidency/provenance.hpp>
#include <nicresidency/records.hpp>
#include <nicresidency/registry.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace nicresidency;

namespace {

Provenance syn() {
  return Provenance(EvidenceKind::kSynthetic, {Source::kSyntheticFixture});
}

NicRecord make_nic(std::uint64_t id, PciAddress pci, NumaNodeId numa) {
  NicRecord nic;
  nic.id = NicId(id);
  nic.generation = NicGeneration(1);
  nic.os_name = "bench-reg-nic" + std::to_string(id);
  nic.os_description = "NIC Residency registry benchmark fixture (synthetic)";
  nic.pci = pci;
  nic.vendor_id = 0x1DB6;
  nic.device_id = 0x1001;
  nic.subsystem_vendor_id = 0x1DB6;
  nic.subsystem_id = 0x2001;
  nic.revision = 1;
  nic.device_kind = DeviceKind::kNic;
  nic.function_class = FunctionClass::kPhysicalFunction;
  nic.capability.set_rdma_capable(true);
  nic.capability.set_offload_capable(true);
  nic.capability.set_accelerator_affinity_supported(true);
  nic.capability.set_generation(CapabilityGeneration(1));
  nic.capability.set_provenance(syn());
  nic.numa_node = numa;
  nic.link_state = LinkState::kUp;
  nic.health = HealthState::kReady;
  nic.lifecycle = LifecycleState::kAvailable;
  nic.provenance = syn();
  return nic;
}

}  // namespace

int main() {
  constexpr std::size_t kNics = 512;
  constexpr std::size_t kSnapIters = 20000;

  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  NicRegistry& reg = eng.registry();
  const CoordinatorEpoch epoch = eng.epoch();

  // Build the NIC records once (fixture creation is not the measured work).
  std::vector<NicRecord> recs;
  recs.reserve(kNics);
  for (std::size_t i = 0; i < kNics; ++i) {
    const std::uint16_t bus = static_cast<std::uint16_t>((i % 2 == 0) ? 0x01 : 0x81);
    const std::uint8_t dev = static_cast<std::uint8_t>(((i / 2) & 0x1F) + 1);
    const std::uint8_t fn = static_cast<std::uint8_t>(i % 8);
    recs.push_back(make_nic(1000 + i, PciAddress(0, static_cast<std::uint8_t>(bus), static_cast<std::uint8_t>(dev), static_cast<std::uint8_t>(fn)), NumaNodeId(i % 2)));
  }

  // --- registration throughput (measured completed work) --------------------
  auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < kNics; ++i) {
    reg.register_nic(recs[i], epoch, w, boot);
  }
  auto t1 = std::chrono::steady_clock::now();
  const double reg_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double reg_ns = reg_ms * 1e6 / static_cast<double>(kNics);

  // --- snapshot() throughput over the populated registry --------------------
  auto s0 = std::chrono::steady_clock::now();
  std::size_t snapshot_total = 0;
  for (std::size_t i = 0; i < kSnapIters; ++i) {
    RegistrySnapshot snap = reg.snapshot();
    snapshot_total += snap.nic_count();
  }
  auto s1 = std::chrono::steady_clock::now();
  const double snap_ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
  const double snap_ns = snap_ms * 1e6 / static_cast<double>(kSnapIters);

  std::printf(
      "bench_registry: %zu iterations in %.3f ms (%.1f ns/op), "
      "context: nics=%zu funcs=0 queues=0 attachments=0 candidates=0 topo_nodes=0\n",
      kNics, reg_ms, reg_ns, kNics);
  std::printf(
      "bench_registry_snapshot: %zu iterations in %.3f ms (%.1f ns/op), "
      "snapshot_nics_seen=%zu\n",
      kSnapIters, snap_ms, snap_ns, snapshot_total);
  return 0;
}
