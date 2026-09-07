// Real worker-death + coordinator-restart authority proofs.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "harness.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include "nicresidency/authority.hpp"
#include "nicresidency/engine.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/registry.hpp"
#include "nicresidency/persistence.hpp"
#include "nicresidency/version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace nicresidency;

namespace {

#ifdef _WIN32
HANDLE g_worker = INVALID_HANDLE_VALUE;

std::string worker_exe_path() {
  char buf[MAX_PATH] = {0};
  ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p(buf);
  std::size_t slash = p.find_last_of('\\');
  std::string dir = (slash == std::string::npos) ? std::string() : p.substr(0, slash + 1);
  return dir + "nic_authority_worker.exe";
}

bool spawn_worker(const std::string& framePath, const std::string& workerId, const std::string& gen) {
  std::string exe = worker_exe_path();
  std::string cmdline;
  cmdline += char(34); cmdline += exe; cmdline += char(34); cmdline += char(32);
  cmdline += char(34); cmdline += framePath; cmdline += char(34); cmdline += char(32);
  cmdline += workerId; cmdline += char(32); cmdline += gen;
  std::vector<char> cbuf(cmdline.begin(), cmdline.end()); cbuf.push_back(char(0));
  ::DeleteFileA(framePath.c_str());
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!::CreateProcessA(nullptr, cbuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  g_worker = pi.hProcess;
  ::CloseHandle(pi.hThread);
  for (int i = 0; i < 400; ++i) {
    ::Sleep(50);
    std::ifstream f(framePath, std::ios::binary);
    if (f.good()) { std::ostringstream ss; ss << f.rdbuf(); if (!ss.str().empty()) return true; }
  }
  return false;
}

void kill_worker() {
  if (g_worker != INVALID_HANDLE_VALUE) {
    ::TerminateProcess(g_worker, 0);
    ::WaitForSingleObject(g_worker, 10000);
    ::CloseHandle(g_worker);
    g_worker = INVALID_HANDLE_VALUE;
  }
}
#endif

bool read_frame(const std::string& p, WorkerId& w, WorkerBootId& b, CoordinatorEpoch& e, NicRecord& nic) {
  std::ifstream f(p, std::ios::binary); if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf();
  std::string frame = ss.str();
  ProtocolMessageType t; std::string payload; std::size_t consumed = 0;
  try {
    if (!FrameCodec::try_decode(frame.data(), frame.size(), t, payload, consumed)) return false;
    return FrameCodec::parse_publish_nic(payload, w, b, e, nic);
  } catch (const ResidencyError&) { return false; }
}

}  // namespace

NRTEST(worker_death_real_process) {
#ifndef _WIN32
  NREXPECT(true);
#else
  const char* fa = "nic_auth_a.bin"; const char* fb = "nic_auth_b.bin";
  ::DeleteFileA(fa); ::DeleteFileA(fb);
  bool spawnedA = spawn_worker(fa, "1", "1");
  NREXPECT_MSG(spawnedA, "worker A did not produce a frame");
  WorkerId w; WorkerBootId bA; CoordinatorEpoch e; NicRecord nic;
  NREXPECT(read_frame(fa, w, bA, e, nic));

  NicRegistry reg(CoordinatorEpoch(1));
  reg.register_worker(w, bA);
  reg.register_nic(nic, CoordinatorEpoch(1), w, bA);
  ResidencyRecord rr;
  rr.id = ResidencyId(1); rr.generation = ResidencyGeneration(1); rr.nic = nic.id;
  rr.state = ResidencyState::kResident;
  rr.owner_worker = w; rr.owner_boot = bA; rr.epoch = CoordinatorEpoch(1);
  rr.lifecycle = LifecycleState::kResident; rr.provenance = nic.provenance; rr.freshness = 1;
  reg.publish_residency(rr, CoordinatorEpoch(1), w, bA);
  {
    RegistrySnapshot sn = reg.snapshot();
    ResidencyPolicy policy; policy.set_allow_synthetic(true);
    ResidencyDecision d = policy.decide(sn, GpuId(), NumaNodeId(1), EndpointId(), w);
    NREXPECT(d.selected.valid()); NREXPECT(d.selected == nic.id);
  }

  kill_worker();
  reg.worker_retired(w, bA);
  {
    RegistrySnapshot sn = reg.snapshot();
    const NicRecord* n = sn.nic(nic.id);
    NREXPECT(n != nullptr);
    NREXPECT(n->lifecycle == LifecycleState::kRevalidationRequired);
    ResidencyPolicy policy; policy.set_allow_synthetic(true);
    ResidencyDecision d = policy.decide(sn, GpuId(), NumaNodeId(1), EndpointId(), w);
    NREXPECT(!d.selected.valid());
    NREXPECT(d.overall == ResidencyEligibility::kRevalidationRequired || d.overall == ResidencyEligibility::kStaleDevice);
  }

  WorkerBootId bA2 = WorkerBootId(bA.value() ^ 0x9E3779B97F4A7C15ULL);
  bool staleRejected = false;
  try { reg.register_worker(w, bA2); reg.register_nic(nic, CoordinatorEpoch(1), w, bA); }
  catch (const ResidencyError& err) { staleRejected = (err.code() == ErrorCode::kStaleBoot); }
  NREXPECT(staleRejected);
  reg.worker_retired(WorkerId(1), bA2);  // clear the bA2 incarnation before B

  bool spawnedB = spawn_worker(fb, "1", "2");
  NREXPECT_MSG(spawnedB, "worker B did not produce a frame");
  WorkerId w2; WorkerBootId bB; CoordinatorEpoch e2; NicRecord nic2;
  NREXPECT(read_frame(fb, w2, bB, e2, nic2));
  NREXPECT(bB != bA);
  reg.register_worker(w2, bB);
  reg.register_nic(nic2, CoordinatorEpoch(1), w2, bB);
  {
    RegistrySnapshot sn = reg.snapshot();
    const NicRecord* n = sn.nic(nic.id);
    NREXPECT(n != nullptr);
    NREXPECT(n->lifecycle != LifecycleState::kRevalidationRequired);
    ResidencyPolicy policy; policy.set_allow_synthetic(true);
    ResidencyDecision d = policy.decide(sn, GpuId(), NumaNodeId(1), EndpointId(), w2);
    NREXPECT(d.selected.valid()); NREXPECT(d.selected == nic.id);
  }
  kill_worker();
  ::DeleteFileA(fa); ::DeleteFileA(fb);
#endif
}

NRTEST(coordinator_restart_epoch_advance) {
  const char* path = "nic_crd_restart.ner";
  std::remove(path);
  NicRecord nic;
  nic.id = NicId(0x777); nic.generation = NicGeneration(3);
  nic.os_name = "durable-nic"; nic.pci = PciAddress(0, 0x02, 0, 0);
  nic.numa_node = NumaNodeId(1);
  nic.provenance = Provenance(EvidenceKind::kReal, {Source::kPci});
  nic.lifecycle = LifecycleState::kResident; nic.freshness = 9;
  {
    NicRegistry reg(CoordinatorEpoch(5));
    WorkerId w(1); WorkerBootId b = fresh_boot_id();
    reg.register_worker(w, b);
    reg.register_nic(nic, CoordinatorEpoch(5), w, b);
    PersistentState st; st.format_version = kPersistenceFormatVersion;
    st.epoch = CoordinatorEpoch(5); st.nics.push_back(nic);
    PersistenceStore(path).save(st);
  }
  NicRegistry reg2(CoordinatorEpoch(6));
  RecoveryResult rec = PersistenceStore(path).recover();
  NREXPECT(rec.ok); NREXPECT(rec.epoch == CoordinatorEpoch(5)); NREXPECT(rec.nics.size() == 1);
  NREXPECT(rec.nics[0].lifecycle == LifecycleState::kRevalidationRequired);
  NREXPECT(rec.nics[0].freshness == 0);
  WorkerBootId b6 = fresh_boot_id();
  reg2.register_worker(WorkerId(1), b6);
  bool staleEpochRejected = false;
  try { reg2.register_nic(rec.nics[0], CoordinatorEpoch(5), WorkerId(1), b6); }
  catch (const ResidencyError& err) { staleEpochRejected = (err.code() == ErrorCode::kStaleEpoch); }
  NREXPECT(staleEpochRejected);
  NicRecord fresh = rec.nics[0];
  fresh.lifecycle = LifecycleState::kAvailable; fresh.freshness = 1; fresh.generation = NicGeneration(4);
  reg2.register_nic(fresh, CoordinatorEpoch(6), WorkerId(1), b6);
  RegistrySnapshot sn = reg2.snapshot();
  const NicRecord* n = sn.nic(NicId(0x777));
  NREXPECT(n != nullptr); NREXPECT(n->generation == NicGeneration(4));
  std::remove(path);
}

int main() { return nrtest::run_all(); }
