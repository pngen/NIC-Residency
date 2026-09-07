// bench_persistence.cpp
// NIC Residency benchmark: PersistenceStore save() + load() round-trip over a
// moderate (synthetic) NIC set, measuring durable identity serialization and
// validated reload cost.  This is a synthetic fixture persistence workload,
// not a measure of any real hardware or storage device.
//
// NOTE: PersistenceStore::save() atomically replaces the target via std::rename,
// which cannot overwrite an existing file on Windows.  To allow repeated
// round-trips on the same path we remove the file after each load (this unlink
// is performed outside the timed window so it is not counted in the result).
//
// SPDX-License-Identifier: Apache-2.0
#include <nicresidency/capability.hpp>
#include <nicresidency/enums.hpp>
#include <nicresidency/locality.hpp>
#include <nicresidency/persistence.hpp>
#include <nicresidency/provenance.hpp>
#include <nicresidency/records.hpp>
#include <nicresidency/version.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
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
  nic.os_name = "bench-persist-nic" + std::to_string(id);
  nic.os_description = "NIC Residency persistence benchmark fixture (synthetic)";
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
  nic.lifecycle = LifecycleState::kResident;
  nic.provenance = syn();
  return nic;
}

}  // namespace

int main() {
  constexpr std::size_t kNics = 256;
  constexpr int kIters = 200;
  const char* path = "bench_persist_state.ner";

  try {
    std::remove(path);

    PersistentState st;
    st.format_version = kPersistenceFormatVersion;
    st.epoch = CoordinatorEpoch(1);
    st.policy.allow_synthetic = true;
    st.policy.require_rdma = true;
    st.policy.require_ready = true;
    st.policy.freshness_limit = 1024;
    st.nics.reserve(kNics);
    for (std::size_t i = 0; i < kNics; ++i) {
      const std::uint16_t bus = static_cast<std::uint16_t>((i % 2 == 0) ? 0x01 : 0x81);
      const std::uint8_t dev = static_cast<std::uint8_t>(((i / 2) & 0x1F) + 1);
      const std::uint8_t fn = static_cast<std::uint8_t>(i % 8);
      st.nics.push_back(make_nic(3000 + i, PciAddress(0, static_cast<std::uint8_t>(bus), static_cast<std::uint8_t>(dev), static_cast<std::uint8_t>(fn)), NumaNodeId(i % 2)));
    }

    PersistenceStore store(path);

    // Warm up once so the file is created, then clear it so the first timed
    // save has no pre-existing target to atomically replace.
    store.save(st);
    std::remove(path);

    // --- save + load round-trip throughput (measured completed work) --------
    std::size_t loaded_total = 0;
    double total_ms = 0.0;
    for (int i = 0; i < kIters; ++i) {
      auto a = std::chrono::steady_clock::now();
      store.save(st);
      PersistentState loaded = store.load();
      auto b = std::chrono::steady_clock::now();
      total_ms += std::chrono::duration<double, std::milli>(b - a).count();
      loaded_total += loaded.nics.size();
      // Keep the next save's rename target clear (excluded from timing).
      std::remove(path);
    }

    // Two operations (save + load) complete per iteration.
    const double ns = total_ms * 1e6 / (static_cast<double>(kIters) * 2.0);

    std::printf(
        "bench_persistence: %d iterations in %.3f ms (%.1f ns/op, save+load), "
        "context: nics=%zu funcs=0 queues=0 attachments=0 candidates=0 "
        "topo_nodes=0 (synthetic) loaded_nics=%zu\n",
        kIters, total_ms, ns, kNics, loaded_total);

    std::remove(path);
    return 0;
  } catch (const std::exception& e) {
    std::printf("bench_persistence: FAILED - %s\n", e.what());
    std::remove(path);
    return 1;
  }
}
