// bench_queue.cpp
// NIC Residency benchmark: register many QueueRecord under functions and
// measure QueueRecord lookup by id on a RegistrySnapshot.  The workload is
// built directly via NicRegistry under a host worker (synthetic fixture).
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
  nic.os_name = "bench-queue-nic" + std::to_string(id);
  nic.os_description = "NIC Residency queue benchmark fixture (synthetic)";
  nic.pci = pci;
  nic.vendor_id = 0x1DB6;
  nic.device_id = 0x1001;
  nic.device_kind = DeviceKind::kNic;
  nic.function_class = FunctionClass::kPhysicalFunction;
  nic.capability.set_rdma_capable(true);
  nic.capability.set_generation(CapabilityGeneration(1));
  nic.capability.set_provenance(syn());
  nic.numa_node = numa;
  nic.link_state = LinkState::kUp;
  nic.health = HealthState::kReady;
  nic.lifecycle = LifecycleState::kAvailable;
  nic.provenance = syn();
  return nic;
}

FunctionRecord make_function(std::uint64_t id, NicId nic, NumaNodeId numa,
                             FunctionClass fc) {
  FunctionRecord fn;
  fn.id = FunctionId(id);
  fn.generation = FunctionGeneration(1);
  fn.parent_nic = nic;
  fn.function_class = fc;
  fn.numa_node = numa;
  fn.lifecycle = LifecycleState::kAvailable;
  fn.provenance = syn();
  return fn;
}

QueueRecord make_queue(std::uint64_t id, FunctionId func, NicId nic, std::string kind) {
  QueueRecord q;
  q.id = QueueId(id);
  q.generation = QueueGeneration(1);
  q.parent_function = func;
  q.parent_nic = nic;
  q.kind = std::move(kind);
  q.lifecycle = LifecycleState::kAvailable;
  q.provenance = syn();
  return q;
}

}  // namespace

int main() {
  constexpr std::size_t kNicsPerStat = 100;   // distinct NICs
  constexpr int kFuncPerNic = 2;              // functions per NIC
  constexpr int kQueuePerFunc = 20;           // queues per function
  constexpr std::size_t kLookupIters = 1000000;

  const std::size_t nics = kNicsPerStat;
  const std::size_t funcs = nics * static_cast<std::size_t>(kFuncPerNic);
  const std::size_t queues = funcs * static_cast<std::size_t>(kQueuePerFunc);

  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  NicRegistry& reg = eng.registry();
  const CoordinatorEpoch epoch = eng.epoch();

  std::vector<QueueId> qids;
  qids.reserve(queues);

  std::uint64_t queue_counter = 100000;
  for (std::size_t i = 0; i < nics; ++i) {
    const NicId nic(1000 + static_cast<std::uint64_t>(i));
    const NumaNodeId numa(i % 2);
    const std::uint16_t bus = static_cast<std::uint16_t>((i % 2 == 0) ? 0x01 : 0x81);
    const std::uint8_t dev = static_cast<std::uint8_t>(((i / 2) & 0x1F) + 1);
    reg.register_nic(make_nic(nic.value(), PciAddress(0, static_cast<std::uint8_t>(bus), static_cast<std::uint8_t>(dev), 0), numa),
                     epoch, w, boot);

    for (int f = 0; f < kFuncPerNic; ++f) {
      const std::uint64_t func_id = 5000 + i * static_cast<std::size_t>(kFuncPerNic) +
                                    static_cast<std::uint64_t>(f);
      const FunctionId func(func_id);
      reg.register_function(
          make_function(func_id, nic, numa,
                        f == 0 ? FunctionClass::kPhysicalFunction
                               : FunctionClass::kVirtualFunction),
          epoch, w, boot);

      for (int q = 0; q < kQueuePerFunc; ++q) {
        const std::uint64_t qid = queue_counter++;
        reg.register_queue(
            make_queue(qid, func, nic, q % 3 == 0 ? "RDMA" : q % 3 == 1 ? "RX" : "COMPLETION"),
            epoch, w, boot);
        qids.push_back(QueueId(qid));
      }
    }
  }

  RegistrySnapshot snap = reg.snapshot();
  const std::size_t snap_queues = snap.queue_count();

  // --- queue lookup by id (measured completed work) -------------------------
  std::uint64_t found = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < kLookupIters; ++i) {
    const QueueId id = qids[(i * 7) % qids.size()];
    const QueueRecord* q = snap.queue(id);
    if (q != nullptr) found += q->id.value();
  }
  auto t1 = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double ns = ms * 1e6 / static_cast<double>(kLookupIters);

  std::printf(
      "bench_queue: %zu iterations in %.3f ms (%.1f ns/op), "
      "context: nics=%zu funcs=%zu queues=%zu attachments=0 candidates=0 "
      "topo_nodes=0 (synthetic) snapshot_queues=%zu\n",
      kLookupIters, ms, ns, nics, funcs, queues, snap_queues);
  std::printf("bench_queue_checksum: %llu\n", static_cast<unsigned long long>(found));
  return 0;
}
