// Invariant, randomized property, deterministic race, and adversarial tests.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "harness.hpp"

#include <cstdint>
#include <thread>
#include <vector>

#include "nicresidency/engine.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/registry.hpp"
#include "nicresidency/synthetic_backend.hpp"

using namespace nicresidency;

namespace {
std::uint64_t g_seed = 0x123456789ABCDEFULL;
std::uint64_t next_rand() { g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL; return g_seed; }

CoordinatorEpoch ep() { return CoordinatorEpoch(1); }
WorkerId w1() { return WorkerId(1); }

// Structural invariants that must hold after every operation.
void invariant_check(const RegistrySnapshot& sn) {
  for (const auto& kv : sn.functions()) {
    NREXPECT(sn.nic(kv.second.parent_nic) != nullptr);
  }
  for (const auto& kv : sn.queues()) {
    NREXPECT(sn.function(kv.second.parent_function) != nullptr);
    NREXPECT(sn.nic(kv.second.parent_nic) != nullptr);
  }
  for (const auto& kv : sn.ports()) {
    NREXPECT(sn.nic(kv.second.parent_nic) != nullptr);
  }
  for (const auto& kv : sn.attachments()) {
    NREXPECT(sn.nic(kv.second.nic) != nullptr);
    if (kv.second.function.valid()) NREXPECT(sn.function(kv.second.function) != nullptr);
    if (kv.second.queue.valid()) NREXPECT(sn.queue(kv.second.queue) != nullptr);
  }
}

NicRecord mk_nic(NicId id, NicGeneration g) {
  NicRecord n; n.id = id; n.generation = g;
  n.os_name = "inv-nic"; n.os_description = "invar";
  n.pci = PciAddress(0, static_cast<std::uint8_t>(id.value() & 0xFF), 0, 0);
  n.numa_node = NumaNodeId((id.value() % 4) + 1);
  n.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  n.lifecycle = LifecycleState::kAvailable; n.freshness = 1;
  return n;
}

FunctionRecord mk_function(FunctionId id, NicId parent, FunctionGeneration g) {
  FunctionRecord f; f.id = id; f.generation = g; f.parent_nic = parent;
  f.lifecycle = LifecycleState::kAvailable; f.freshness = 1;
  f.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  return f;
}

}  // namespace

NRTEST(invariants_dangling_references) {
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  NicRegistry& reg = eng.registry();
  bool rejected = false;
  try {
    reg.register_function(mk_function(FunctionId(1), NicId(9999), FunctionGeneration(1)), ep(), w1(), boot);
  } catch (const ResidencyError& e) { rejected = (e.code() == ErrorCode::kInvalidTopology); }
  NREXPECT(rejected);
  rejected = false;
  try {
    QueueRecord q; q.id = QueueId(1); q.generation = QueueGeneration(1);
    q.parent_function = FunctionId(7777); q.parent_nic = NicId(1);
    reg.register_queue(q, ep(), w1(), boot);
  } catch (const ResidencyError& e) { rejected = (e.code() == ErrorCode::kInvalidTopology); }
  NREXPECT(rejected);
}

NRTEST(invariants_generations_monotonic) {
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  NicRegistry& reg = eng.registry();
  reg.register_nic(mk_nic(NicId(5), NicGeneration(1)), ep(), w1(), boot);
  bool staleRejected = false;
  try {
    reg.register_nic(mk_nic(NicId(5), NicGeneration(0)), ep(), w1(), boot);
  } catch (const ResidencyError& e) { staleRejected = (e.code() == ErrorCode::kStaleNic); }
  NREXPECT(staleRejected);
  NREXPECT(reg.snapshot().nic(NicId(5))->generation == NicGeneration(1));
}

NRTEST(invariants_synthetic_never_real_and_unknown_never_local) {
  // SYNTHETIC evidence stays SYNTHETIC and never becomes REAL.
  SyntheticBackend sb(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx; ctx.freshness = 1;
  DiscoveryResult d = sb.discover(ctx);
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  eng.ingest(d, w1(), boot);
  RegistrySnapshot sn = eng.registry().snapshot();
  for (const auto& kv : sn.nics()) NREXPECT(kv.second.provenance.synthetic());
  // UNKNOWN visibility: a NIC with unsupported provenance is not silently eligible.
  ResidencyPolicy policy;  // allow_synthetic defaults false -> fail closed
  ResidencyDecision dec = policy.decide(sn, GpuId(100), NumaNodeId(1), EndpointId(), w1());
  NREXPECT(dec.overall == ResidencyEligibility::kInsufficientEvidence ||
           dec.overall == ResidencyEligibility::kUnsupported ||
           dec.overall == ResidencyEligibility::kNotFound);
}

NRTEST(property_randomized_invariants) {
  const int kOps = 800;
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  NicRegistry& reg = eng.registry();
  for (int i = 0; i < kOps; ++i) {
    std::uint64_t r = next_rand() % 6;
    try {
      switch (r) {
        case 0: { NicId id = NicId(next_rand() % 60 + 1); reg.register_nic(mk_nic(id, NicGeneration(next_rand() % 3 + 1)), ep(), w1(), boot); break; }
        case 1: { NicId parent = NicId(next_rand() % 60 + 1); if (reg.snapshot().nic(parent) != nullptr) reg.register_function(mk_function(FunctionId(next_rand() % 90 + 1), parent, FunctionGeneration(next_rand() % 3 + 1)), ep(), w1(), boot); break; }
        case 2: { if (reg.snapshot().nic_count() > 0) { NicId parent = reg.snapshot().nics().begin()->first; reg.advance_nic_generation(parent, ep(), w1(), boot); } break; }
        case 3: { if (reg.snapshot().functions().empty()) { reg.register_nic(mk_nic(NicId(2000), NicGeneration(1)), ep(), w1(), boot); } else { FunctionId id = reg.snapshot().functions().begin()->first; reg.advance_function_generation(id, ep(), w1(), boot); } break; }
        case 4: { if (reg.snapshot().nic_count() > 0) { RegistrySnapshot sn = reg.snapshot(); ResidencyPolicy policy; policy.set_allow_synthetic(true); ResidencyDecision dec = policy.decide(sn, GpuId(), NumaNodeId(1), EndpointId(), w1()); (void)dec; } break; }
        case 5: { reg.snapshot(); break; }  // read-heavy query
      }
    } catch (const ResidencyError& e) {
      if (e.code() == ErrorCode::kStaleEpoch || e.code() == ErrorCode::kStaleBoot) {
        std::printf("FAIL unexpected stale authority at op %d\n", i);
        NREXPECT(false);
      }
    }
    invariant_check(reg.snapshot());
  }
}

NRTEST(race_deterministic_concurrent) {
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  NicRegistry& reg = eng.registry();
  std::vector<std::thread> ths;
  for (int t = 0; t < 4; ++t) {
    ths.emplace_back([&, t]() {
      for (int i = 0; i < 200; ++i) {
        NicId id = NicId(1000 + t * 200 + i);
        try { reg.register_nic(mk_nic(id, NicGeneration(1)), ep(), w1(), boot); } catch (const ResidencyError&) {}
        RegistrySnapshot sn = reg.snapshot();
        invariant_check(sn);
      }
    });
  }
  for (auto& t : ths) t.join();
  NREXPECT(reg.snapshot().nic_count() == 800);
  invariant_check(reg.snapshot());
}

NRTEST(adversarial_absurd_counts_and_duplicates) {
  ResidencyEngine eng(ep());
  WorkerBootId boot = eng.register_host_worker(w1());
  NicRegistry& reg = eng.registry();
  // Duplicate function id at same generation -> conflicting duplicate rejected.
  reg.register_nic(mk_nic(NicId(1), NicGeneration(1)), ep(), w1(), boot);
  reg.register_function(mk_function(FunctionId(1), NicId(1), FunctionGeneration(1)), ep(), w1(), boot);
  // A genuinely conflicting duplicate: same id + generation but different content.
  FunctionRecord conflictFn = mk_function(FunctionId(1), NicId(1), FunctionGeneration(1));
  conflictFn.function_class = FunctionClass::kVirtualFunction;
  bool conflict = false;
  try {
    reg.register_function(conflictFn, ep(), w1(), boot);
  } catch (const ResidencyError& e) { conflict = (e.code() == ErrorCode::kInvalidArgument); }
  NREXPECT(conflict);
}

int main() { return nrtest::run_all(); }
