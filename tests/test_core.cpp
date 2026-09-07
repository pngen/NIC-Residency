
#include "harness.hpp"

#include "nicresidency/engine.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/synthetic_backend.hpp"
#include "nicresidency/persistence.hpp"
#include "nicresidency/version.hpp"
#include <cstdio>
#include <fstream>

using namespace nicresidency;

NRTEST(synthetic_one_nic_local_to_gpu) {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  SyntheticBackend sb(SyntheticScenario::kOneNicLocalToOneGpu);
  DiscoveryContext ctx;
  ctx.freshness = 1;
  DiscoveryResult d = sb.discover(ctx);
  eng.ingest(d, w, boot);

  RegistrySnapshot snap = eng.registry().snapshot();
  NREXPECT(snap.nic_count() == 1);
  NREXPECT(snap.gpu_count() == 1);
  NREXPECT(snap.topology() != nullptr);

  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);
  policy.set_policy_id(PolicyId(7));
  ResidencyDecision dec = policy.decide(snap, GpuId(100), NumaNodeId(), EndpointId(), w);
  NREXPECT(dec.selected.valid());
  NREXPECT(dec.selected == NicId(10));
  NREXPECT(dec.overall == ResidencyEligibility::kResidencyAllowed);
}

NRTEST(registry_rejects_stale_epoch) {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  NicRecord nic;
  nic.id = NicId(5);
  nic.generation = NicGeneration(1);
  bool threw = false;
  try {
    eng.registry().register_nic(nic, CoordinatorEpoch(99), w, boot);
  } catch (const ResidencyError& err) {
    threw = (err.code() == ErrorCode::kStaleEpoch);
  }
  NREXPECT(threw);
}


NRTEST(persistence_roundtrip_and_revalidation) {
  PersistentState st;
  st.format_version = kPersistenceFormatVersion;
  st.epoch = CoordinatorEpoch(3);
  NicRecord nic;
  nic.id = NicId(42); nic.generation = NicGeneration(2);
  nic.os_name = "test-nic"; nic.os_description = "desc";
  nic.pci = PciAddress(0, 0x03, 0x12, 0);
  nic.vendor_id = 0x10EC; nic.device_id = 0x8125;
  nic.device_kind = DeviceKind::kNic; nic.function_class = FunctionClass::kUnknown;
  nic.capability.set_rdma_capable(true);
  nic.capability.set_generation(CapabilityGeneration(1));
  nic.capability.set_provenance(Provenance(EvidenceKind::kReal, {Source::kPci}));
  nic.numa_node = NumaNodeId(2);
  nic.provenance = Provenance(EvidenceKind::kReal, {Source::kOperatingSystem});
  nic.link_state = LinkState::kUp; nic.health = HealthState::kReady;
  nic.lifecycle = LifecycleState::kResident;
  st.nics.push_back(nic);
  const char* path = "nic_persist_test.ner";
  std::remove(path);
  PersistenceStore store(path);
  store.save(st);
  PersistentState loaded = store.load();
  NREXPECT(loaded.nics.size() == 1);
  NREXPECT(loaded.nics[0].id == NicId(42));
  NREXPECT(loaded.nics[0].generation == NicGeneration(2));
  NREXPECT(loaded.nics[0].pci == PciAddress(0, 0x03, 0x12, 0));
  NREXPECT(loaded.nics[0].os_name == "test-nic");
  NREXPECT(loaded.nics[0].lifecycle == LifecycleState::kRevalidationRequired);
  NREXPECT(loaded.nics[0].link_state == LinkState::kUnknown);
  NREXPECT(loaded.epoch == CoordinatorEpoch(3));
  RecoveryResult rr = store.recover();
  NREXPECT(rr.ok);
  NREXPECT(rr.nics.size() == 1);
  std::remove(path);
}

NRTEST(persistence_rejects_corruption) {
  const char* path = "nic_persist_corrupt.ner";
  std::remove(path);
  PersistenceStore store(path);
  PersistentState st; st.epoch = CoordinatorEpoch(1);
  store.save(st);
  {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << "garbage-garbage-garbage-garbage";
  }
  bool threw = false;
  try { store.load(); } catch (const ResidencyError& e) {
    threw = true;
    NREXPECT(e.code() == ErrorCode::kIntegrityFailure);
  }
  NREXPECT(threw);
  std::remove(path);
}

NRTEST(persistence_rejects_trailing_garbage) {
  const char* path = "nic_persist_trail.ner";
  std::remove(path);
  PersistenceStore store(path);
  PersistentState st; st.epoch = CoordinatorEpoch(5);
  store.save(st);
  // Append trailing bytes after the checksum, then load (must reject).
  {
    std::ofstream f(path, std::ios::binary | std::ios::app);
    f << "EXTRA";
  }
  bool threw = false;
  try { store.load(); } catch (const ResidencyError& e) {
    threw = true;
    NREXPECT(e.code() == ErrorCode::kIntegrityFailure);
  }
  NREXPECT(threw);
  std::remove(path);
}

int main() { return nrtest::run_all(); }