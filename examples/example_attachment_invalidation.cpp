// example_attachment_invalidation.cpp
//
// Attachment invalidation on topology change: an attachment is published, then
// the topology snapshot advances a generation.  Because a topology change
// invalidates locality evidence, the attachment must become STALE.
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
  WorkerBootId boot = eng.register_host_worker(worker);

  SyntheticBackend backend(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = backend.discover(ctx);
  eng.ingest(d, worker, boot);

  // Publish a governed attachment: NIC 10 / function 10 / port 10 / queue 10
  // bound to GPU 100 on the current topology generation.
  Attachment att;
  att.id = AttachmentId(1);
  att.generation = AttachmentGeneration(1);
  att.nic = NicId(10);
  att.function = FunctionId(10);
  att.port = PortId(10);
  att.queue = QueueId(10);
  att.gpu = GpuId(100);
  att.numa = NumaNodeId();
  att.topology_generation = TopologyGeneration(1);
  att.state = AttachmentState::kCommitted;
  att.owner_worker = worker;
  att.owner_boot = boot;
  att.epoch = eng.epoch();
  att.provenance = Provenance(EvidenceKind::kSynthetic, {Source::kSyntheticFixture});
  att.freshness = 1;
  eng.registry().register_attachment(att, eng.epoch(), worker, boot);

  RegistrySnapshot before = eng.registry().snapshot();
  const Attachment* a0 = before.attachment(AttachmentId(1));
  if (a0 != nullptr) {
    std::printf("Attachment id=%s state=%s (topology gen %s)\n",
                a0->id.str().c_str(), to_string(a0->state),
                a0->topology_generation.str().c_str());
  }

  // Change the topology: a new TopologySnapshot at generation 2.
  DiscoveryContext ctx2;
  ctx2.freshness = 1;
  ctx2.topology_generation = TopologyGeneration(2);
  DiscoveryResult d2 = backend.discover(ctx2);
  eng.registry().set_topology(*d2.topology, eng.epoch(), worker, boot);

  RegistrySnapshot after = eng.registry().snapshot();
  const Attachment* a1 = after.attachment(AttachmentId(1));
  if (a1 != nullptr) {
    std::printf("Attachment id=%s state=%s (topology gen %s)\n",
                a1->id.str().c_str(), to_string(a1->state),
                a1->topology_generation.str().c_str());
  }

  const bool ok = (a1 != nullptr && a1->state == AttachmentState::kStale);
  std::printf("\n%s: the attachment became STALE after the topology change\n",
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
