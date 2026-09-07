// example_worker_incarnation.cpp
//
// Worker incarnation and authority: registering the same worker id with a
// different boot id must raise OWNER_CONFLICT, and retiring a worker whose boot
// id is now stale must mark the dynamic (authority-owned) state as
// REVALIDATION_REQUIRED rather than silently dropping it.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>

#include <nicresidency/engine.hpp>
#include <nicresidency/registry.hpp>
#include <nicresidency/records.hpp>
#include <nicresidency/synthetic_backend.hpp>

using namespace nicresidency;

int main() {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId worker(1);
  WorkerBootId bootA = eng.register_host_worker(worker);  // incarnation A

  SyntheticBackend backend(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, bootA);

  // A different boot id for the same worker id is an owner conflict.
  const WorkerBootId bootB(bootA.value() + 1);
  bool conflict = false;
  try {
    eng.registry().register_worker(worker, bootB);
  } catch (const ResidencyError& err) {
    conflict = (err.code() == ErrorCode::kOwnerConflict);
    std::printf("register_worker(boot B) threw: %s\n", err.what());
  }
  std::printf("\n%s: different boot id for the same worker is OWNER_CONFLICT\n",
              conflict ? "PASS" : "FAIL");
  if (!conflict) {
    return 1;
  }

  // Publish a residency owned by incarnation A.
  ResidencyRecord rr;
  rr.id = ResidencyId(1);
  rr.generation = ResidencyGeneration(1);
  rr.nic = NicId(10);
  rr.function = FunctionId(10);
  rr.port = PortId(10);
  rr.queue = QueueId(10);
  rr.target_gpu = GpuId(100);
  rr.state = ResidencyState::kResident;
  rr.topology_generation = TopologyGeneration(1);
  rr.ownership = OwnershipState::kAssigned;
  rr.owner_worker = worker;
  rr.owner_boot = bootA;
  rr.epoch = eng.epoch();
  rr.policy = PolicyId(1);
  rr.provenance = Provenance(EvidenceKind::kSynthetic, {Source::kSyntheticFixture});
  rr.freshness = 1;
  rr.lifecycle = LifecycleState::kResident;
  eng.registry().publish_residency(rr, eng.epoch(), worker, bootA);

  const auto& before = eng.registry().residencies();
  std::printf("Residency before retire: state=%s\n",
              to_string(before.front().state));

  // Retire incarnation A: dynamic state it owned must become revalidation-required.
  eng.registry().worker_retired(worker, bootA);

  const auto& after = eng.registry().residencies();
  std::printf("Residency after retire:  state=%s\n",
              to_string(after.front().state));

  const bool ok = (after.front().state == ResidencyState::kRevalidationRequired);
  std::printf("\n%s: retiring the worker marks its dynamic state REVALIDATION_REQUIRED\n",
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
