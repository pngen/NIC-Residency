// Regression tests: persistence field decoding, generation-advance
// invalidation, residency/attachment republication, publisher binding, and
// coordinator-restart fencing.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "harness.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "nicresidency/persistence.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/registry.hpp"
#include "nicresidency/version.hpp"

using namespace nicresidency;

namespace {

CoordinatorEpoch ep1() { return CoordinatorEpoch(1); }

NicRecord mk_nic(NicId id, NicGeneration g) {
  NicRecord n;
  n.id = id;
  n.generation = g;
  n.os_name = "reg-nic";
  n.os_description = "regression";
  n.pci = PciAddress(0, static_cast<std::uint8_t>(id.value() & 0xFF), 0, 0);
  n.numa_node = NumaNodeId(1);
  n.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  n.lifecycle = LifecycleState::kAvailable;
  n.freshness = 1;
  return n;
}

FunctionRecord mk_function(FunctionId id, NicId parent, FunctionGeneration g) {
  FunctionRecord f;
  f.id = id;
  f.generation = g;
  f.parent_nic = parent;
  f.lifecycle = LifecycleState::kAvailable;
  f.freshness = 1;
  f.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  return f;
}

QueueRecord mk_queue(QueueId id, FunctionId fn, NicId nic, QueueGeneration g) {
  QueueRecord q;
  q.id = id;
  q.generation = g;
  q.parent_function = fn;
  q.parent_nic = nic;
  q.kind = "RX";
  q.lifecycle = LifecycleState::kAvailable;
  q.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  q.freshness = 1;
  return q;
}

Attachment mk_attachment(AttachmentId id, NicId nic, FunctionId fn, QueueId q, WorkerId w,
                         WorkerBootId b) {
  Attachment a;
  a.id = id;
  a.generation = AttachmentGeneration(1);
  a.nic = nic;
  a.function = fn;
  a.queue = q;
  a.topology_generation = TopologyGeneration(1);
  a.state = AttachmentState::kCommitted;
  a.owner_worker = w;
  a.owner_boot = b;
  a.epoch = ep1();
  a.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  a.freshness = 1;
  return a;
}

ResidencyRecord mk_residency(ResidencyId id, ResidencyGeneration g, NicId nic, FunctionId fn,
                             QueueId q, WorkerId w, WorkerBootId b) {
  ResidencyRecord r;
  r.id = id;
  r.generation = g;
  r.nic = nic;
  r.function = fn;
  r.queue = q;
  r.target_numa = NumaNodeId(1);
  r.state = ResidencyState::kResident;
  r.lifecycle = LifecycleState::kResident;
  r.ownership = OwnershipState::kAssigned;
  r.owner_worker = w;
  r.owner_boot = b;
  r.epoch = ep1();
  r.policy = PolicyId(1);
  r.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  r.freshness = 1;
  return r;
}

// A registry holding NIC 10 / function 20 / queue 30, an attachment and a
// residency over them, all published by worker 1.
struct Fixture {
  NicRegistry reg{ep1()};
  WorkerId w{1};
  WorkerBootId b{0x1111};
  Fixture() {
    reg.register_worker(w, b);
    reg.register_nic(mk_nic(NicId(10), NicGeneration(1)), ep1(), w, b);
    reg.register_function(mk_function(FunctionId(20), NicId(10), FunctionGeneration(1)), ep1(), w, b);
    reg.register_queue(mk_queue(QueueId(30), FunctionId(20), NicId(10), QueueGeneration(1)), ep1(), w, b);
    reg.register_attachment(mk_attachment(AttachmentId(40), NicId(10), FunctionId(20), QueueId(30), w, b),
                            ep1(), w, b);
    reg.publish_residency(mk_residency(ResidencyId(50), ResidencyGeneration(1), NicId(10), FunctionId(20),
                                       QueueId(30), w, b),
                          ep1(), w, b);
  }
  const ResidencyRecord* residency(ResidencyId id) {
    held_ = reg.residencies();
    const ResidencyRecord* found = nullptr;
    for (const auto& r : held_) {
      if (r.id == id) found = &r;
    }
    return found;
  }
  std::vector<ResidencyRecord> held_;
};

bool selects(const NicRegistry& reg, NicId nic, WorkerId w) {
  ResidencyPolicy policy;
  RegistrySnapshot sn = reg.snapshot();
  ResidencyDecision d = policy.decide(sn, GpuId(), NumaNodeId(1), EndpointId(), w);
  return d.selected == nic && (d.overall == ResidencyEligibility::kResidencyAllowed ||
                               d.overall == ResidencyEligibility::kResidencyAllowedDegraded);
}

}  // namespace

// Persisted 16-bit fields (vendor/device ids, PCI segment, string lengths)
// round-trip exactly, including values whose low byte is >= 0x80.
NRTEST(persisted_u16_fields_round_trip) {
  const char* path = "nic_regression_u16.ner";
  for (const std::size_t name_len : {std::size_t{12}, std::size_t{129}}) {
    std::remove(path);
    NicRecord nic = mk_nic(NicId(0x10), NicGeneration(1));
    nic.vendor_id = 0x10DE;
    nic.device_id = 0x2B85;
    nic.subsystem_vendor_id = 0x1043;
    nic.subsystem_id = 0x89E1;
    nic.pci = PciAddress(0x00A1, 0x0E, 0, 0);
    nic.os_name = std::string(name_len, 'n');
    nic.os_description = std::string(name_len + 71, 'd');
    PersistentState st;
    st.format_version = kPersistenceFormatVersion;
    st.epoch = ep1();
    st.nics.push_back(nic);
    PersistenceStore(path).save(st);
    RecoveryResult rec = PersistenceStore(path).recover();
    NREXPECT_MSG(rec.ok, rec.message.c_str());
    NREXPECT(rec.nics.size() == 1);
    if (rec.ok && rec.nics.size() == 1) {
      const NicRecord& r = rec.nics[0];
      NREXPECT(r.vendor_id == 0x10DE);
      NREXPECT(r.device_id == 0x2B85);
      NREXPECT(r.subsystem_vendor_id == 0x1043);
      NREXPECT(r.subsystem_id == 0x89E1);
      NREXPECT(r.pci == nic.pci);
      NREXPECT(r.os_name == nic.os_name);
      NREXPECT(r.os_description == nic.os_description);
    }
  }
  std::remove(path);
}

// A NIC / function / queue republished at a higher generation invalidates its
// dependents exactly like advance_*_generation does.
NRTEST(republished_generation_invalidates_dependents) {
  {
    Fixture f;
    f.reg.register_nic(mk_nic(NicId(10), NicGeneration(2)), ep1(), f.w, f.b);
    RegistrySnapshot sn = f.reg.snapshot();
    NREXPECT(sn.function(FunctionId(20))->lifecycle == LifecycleState::kRevalidationRequired);
    NREXPECT(sn.queue(QueueId(30))->lifecycle == LifecycleState::kRevalidationRequired);
    NREXPECT(sn.attachment(AttachmentId(40))->state == AttachmentState::kStale);
    const ResidencyRecord* r = f.residency(ResidencyId(50));
    NREXPECT(r != nullptr && r->state == ResidencyState::kRevalidationRequired);
  }
  {
    Fixture f;
    f.reg.register_function(mk_function(FunctionId(20), NicId(10), FunctionGeneration(2)), ep1(), f.w, f.b);
    RegistrySnapshot sn = f.reg.snapshot();
    NREXPECT(sn.queue(QueueId(30))->lifecycle == LifecycleState::kRevalidationRequired);
    NREXPECT(sn.attachment(AttachmentId(40))->state == AttachmentState::kStale);
    const ResidencyRecord* r = f.residency(ResidencyId(50));
    NREXPECT(r != nullptr && r->state == ResidencyState::kRevalidationRequired);
  }
  {
    Fixture f;
    f.reg.register_queue(mk_queue(QueueId(30), FunctionId(20), NicId(10), QueueGeneration(2)), ep1(), f.w, f.b);
    const ResidencyRecord* r = f.residency(ResidencyId(50));
    NREXPECT(r != nullptr && r->state == ResidencyState::kRevalidationRequired);
  }
}

// One residency per id: a duplicate does not double-count, a newer generation
// replaces the binding, a conflicting same-generation publication is rejected,
// and retire_residency retires the current binding.
NRTEST(residency_republication_and_retire) {
  Fixture f;
  const ResidencyRecord base =
      mk_residency(ResidencyId(50), ResidencyGeneration(1), NicId(10), FunctionId(20), QueueId(30), f.w, f.b);
  f.reg.publish_residency(base, ep1(), f.w, f.b);  // identical duplicate
  NREXPECT(f.reg.residencies().size() == 1);

  ResidencyRecord newer = base;
  newer.generation = ResidencyGeneration(2);
  f.reg.publish_residency(newer, ep1(), f.w, f.b);
  NREXPECT(f.reg.residencies().size() == 1);
  const ResidencyRecord* cur = f.residency(ResidencyId(50));
  NREXPECT(cur != nullptr && cur->generation == ResidencyGeneration(2));

  ResidencyRecord conflicting = newer;
  conflicting.target_numa = NumaNodeId(2);
  bool rejected = false;
  try {
    f.reg.publish_residency(conflicting, ep1(), f.w, f.b);
  } catch (const ResidencyError& e) {
    rejected = (e.code() == ErrorCode::kInvalidArgument);
  }
  NREXPECT(rejected);

  f.reg.retire_residency(ResidencyId(50), ep1(), f.w, f.b);
  for (const auto& r : f.reg.residencies()) {
    if (r.id == ResidencyId(50)) NREXPECT(r.state == ResidencyState::kRetired);
  }
}

// residencies() is a read surface: it does not alias live registry state.
NRTEST(residency_read_is_a_snapshot) {
  Fixture f;
  const auto& before = f.reg.residencies();
  const std::size_t n0 = before.size();
  f.reg.publish_residency(mk_residency(ResidencyId(51), ResidencyGeneration(1), NicId(10), FunctionId(20),
                                       QueueId(30), f.w, f.b),
                          ep1(), f.w, f.b);
  NREXPECT(before.size() == n0);
  NREXPECT(f.reg.residencies().size() == n0 + 1);
}

// An attachment invalidated by a topology change cannot be restored by
// resending it at the same generation.
NRTEST(stale_attachment_not_restored_at_same_generation) {
  Fixture f;
  const Attachment att = mk_attachment(AttachmentId(40), NicId(10), FunctionId(20), QueueId(30), f.w, f.b);
  f.reg.register_attachment(att, ep1(), f.w, f.b);  // identical duplicate is idempotent
  NREXPECT(f.reg.snapshot().attachment(AttachmentId(40))->state == AttachmentState::kCommitted);
  f.reg.set_topology(TopologySnapshot(TopologyGeneration(2)), ep1(), f.w, f.b);
  NREXPECT(f.reg.snapshot().attachment(AttachmentId(40))->state == AttachmentState::kStale);
  bool rejected = false;
  try {
    f.reg.register_attachment(att, ep1(), f.w, f.b);
  } catch (const ResidencyError& e) {
    rejected = (e.code() == ErrorCode::kInvalidAttachment);
  }
  NREXPECT(rejected);
  NREXPECT(f.reg.snapshot().attachment(AttachmentId(40))->state == AttachmentState::kStale);
}

// A rejected NIC publication does not make the worker a publisher of that NIC:
// its death must not invalidate another live worker's evidence.
NRTEST(rejected_publication_does_not_bind_publisher) {
  NicRegistry reg(ep1());
  const WorkerId w1(1), w2(2);
  const WorkerBootId b1(0x1111), b2(0x2222);
  reg.register_worker(w1, b1);
  reg.register_worker(w2, b2);
  reg.register_nic(mk_nic(NicId(5), NicGeneration(2)), ep1(), w1, b1);
  bool stale = false;
  try {
    reg.register_nic(mk_nic(NicId(5), NicGeneration(1)), ep1(), w2, b2);
  } catch (const ResidencyError& e) {
    stale = (e.code() == ErrorCode::kStaleNic);
  }
  NREXPECT(stale);
  reg.worker_retired(w2, b2);
  NREXPECT(reg.snapshot().nic(NicId(5))->lifecycle == LifecycleState::kAvailable);
  NREXPECT(selects(reg, NicId(5), w1));
}

// A coordinator restart marks prior-epoch NIC evidence revalidation-required:
// it cannot keep backing residency decisions until fresh evidence arrives.
NRTEST(coordinator_restart_fences_prior_epoch_evidence) {
  NicRegistry reg(ep1());
  const WorkerId w(1);
  const WorkerBootId b(0x1111);
  reg.register_worker(w, b);
  reg.register_nic(mk_nic(NicId(7), NicGeneration(1)), ep1(), w, b);
  reg.register_function(mk_function(FunctionId(8), NicId(7), FunctionGeneration(1)), ep1(), w, b);
  NREXPECT(selects(reg, NicId(7), w));

  const CoordinatorEpoch ep2(2);
  reg.coordinator_restart(ep2);
  RegistrySnapshot sn = reg.snapshot();
  NREXPECT(sn.nic(NicId(7))->lifecycle == LifecycleState::kRevalidationRequired);
  NREXPECT(sn.function(FunctionId(8))->lifecycle == LifecycleState::kRevalidationRequired);
  NREXPECT(!selects(reg, NicId(7), w));

  const WorkerBootId b2(0x2222);
  reg.register_worker(w, b2);
  reg.register_nic(mk_nic(NicId(7), NicGeneration(2)), ep2, w, b2);
  NREXPECT(selects(reg, NicId(7), w));
}

// Process death fences everything the worker published, whatever owner the
// record declared: published evidence is bound to the publishing authority.
NRTEST(worker_death_fences_published_evidence) {
  Fixture f;
  Attachment att = mk_attachment(AttachmentId(41), NicId(10), FunctionId(20), QueueId(30), WorkerId(), WorkerBootId());
  f.reg.register_attachment(att, ep1(), f.w, f.b);
  ResidencyRecord rr = mk_residency(ResidencyId(52), ResidencyGeneration(1), NicId(10), FunctionId(20),
                                    QueueId(30), WorkerId(), WorkerBootId());
  rr.epoch = CoordinatorEpoch();
  f.reg.publish_residency(rr, ep1(), f.w, f.b);
  const ResidencyRecord* published = f.residency(ResidencyId(52));
  NREXPECT(published != nullptr && published->owner_worker == f.w && published->owner_boot == f.b &&
           published->epoch == ep1());

  f.reg.worker_retired(f.w, f.b);
  NREXPECT(f.reg.snapshot().attachment(AttachmentId(41))->state == AttachmentState::kStale);
  const ResidencyRecord* r = f.residency(ResidencyId(52));
  NREXPECT(r != nullptr && r->state == ResidencyState::kRevalidationRequired);
}

int main() { return nrtest::run_all(); }
