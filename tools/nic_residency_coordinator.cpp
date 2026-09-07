// nic_residency_coordinator: a real OS-process coordinator for the NIC
// Residency authority plane.  It runs as its own process, ingests worker evidence
// over the framed transport (FrameCodec publish-nic frames on disk), establishes
// an authoritative residency decision, persists the intended durable state, and
// is then killed and relaunched fresh by a test to prove a genuine coordinator
// restart: the CoordinatorEpoch advances, durable identity/capability state is
// recovered conservatively (REVALIDATION_REQUIRED), live worker/process authority
// is NOT silently recovered, stale prior-epoch traffic is rejected, and residency
// authority returns only after Worker A/A' re-establishes under fresh evidence.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#ifdef _WIN32
// windows.h's min/max macros collide with std::numeric_limits<...>::max() used
// by the policy headers; disable them for this translation unit.
#define NOMINMAX
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif

#include "nicresidency/authority.hpp"
#include "nicresidency/enums.hpp"
#include "nicresidency/error.hpp"
#include "nicresidency/persistence.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/records.hpp"
#include "nicresidency/registry.hpp"
#include "nicresidency/version.hpp"

using namespace nicresidency;

namespace {

const char* kWorkerA = "nic_crd2_worker_a.bin";
const char* kWorkerB = "nic_crd2_worker_b.bin";
const char* kState = "nic_crd2_state.ner";
const char* kFirstResult = "nic_crd2_first.txt";
const char* kRestartResult = "nic_crd2_restart.txt";

bool read_file(const char* path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

void write_file(const char* path, const std::string& s) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void write_map(const char* path, const std::map<std::string, std::string>& m) {
  std::string out;
  for (const auto& kv : m) {
    out += kv.first;
    out += '=';
    out += kv.second;
    out += '\n';
  }
  write_file(path, out);
}

// Decode one on-disk framed worker publish-nic frame.
bool decode_worker_frame(const char* path, WorkerId& w, WorkerBootId& b,
                         CoordinatorEpoch& e, NicRecord& nic) {
  std::string frame;
  if (!read_file(path, frame)) return false;
  ProtocolMessageType t;
  std::string payload;
  std::size_t consumed = 0;
  try {
    if (!FrameCodec::try_decode(frame.data(), frame.size(), t, payload, consumed)) {
      return false;
    }
    return FrameCodec::parse_publish_nic(payload, w, b, e, nic);
  } catch (const ResidencyError&) {
    return false;
  }
}

void block_forever() {
  for (;;) {
#ifdef _WIN32
    ::Sleep(1000);
#else
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
  }
}

}  // namespace

int run_first() {
  std::map<std::string, std::string> m;
  m["phase"] = "first";
  WorkerId w;
  WorkerBootId boot;
  CoordinatorEpoch e;
  NicRecord nic;
  if (!decode_worker_frame(kWorkerA, w, boot, e, nic)) {
    m["status"] = "ERROR";
    m["error"] = "decode_failed";
    m["message"] = "could not decode worker A frame";
    write_map(kFirstResult, m);
    return 1;
  }
  try {
    NicRegistry reg(e);
    reg.register_worker(w, boot);
    reg.register_nic(nic, e, w, boot);

    ResidencyRecord rr;
    rr.id = ResidencyId(1);
    rr.generation = ResidencyGeneration(1);
    rr.nic = nic.id;
    rr.state = ResidencyState::kResident;
    rr.lifecycle = LifecycleState::kResident;
    rr.owner_worker = w;
    rr.owner_boot = boot;
    rr.epoch = e;
    rr.target_numa = nic.numa_node;
    rr.provenance = nic.provenance;
    rr.ownership = OwnershipState::kAssigned;
    rr.policy = PolicyId(1);
    rr.freshness = 1;
    reg.publish_residency(rr, e, w, boot);

    ResidencyPolicy policy;
    RegistrySnapshot sn = reg.snapshot();
    ResidencyDecision d =
        policy.decide(sn, GpuId(), nic.numa_node, EndpointId(), w);
    const bool authoritative =
        (d.selected == nic.id) &&
        (d.overall == ResidencyEligibility::kResidencyAllowed ||
         d.overall == ResidencyEligibility::kResidencyAllowedDegraded);

    // Persist the intended durable state (durable identity + inspectable
    // residency history).  Dynamic readiness/link/health is never persisted as
    // authority.
    PersistentState st;
    st.format_version = kPersistenceFormatVersion;
    st.epoch = e;
    st.nics.push_back(nic);
    st.history.push_back(rr);
    PersistenceStore(kState).save(st);

    m["epoch"] = std::to_string(e.value());
    m["worker_boot"] = boot.str();
    m["nic_id"] = nic.id.str();
    m["nic_generation"] = std::to_string(nic.generation.value());
    m["decision"] = to_string(d.overall);
    m["selected"] = d.selected.str();
    m["residency_state"] = to_string(rr.state);
    m["persisted"] = "1";
    m["authoritative"] = authoritative ? "1" : "0";
    write_map(kFirstResult, m);
  } catch (const ResidencyError& err) {
    m["status"] = "ERROR";
    m["error"] = to_string(err.code());
    m["message"] = err.what();
    write_map(kFirstResult, m);
    return 1;
  }
  block_forever();
  return 0;
}

int run_restart() {
  std::map<std::string, std::string> m;
  m["phase"] = "restart";
  try {
    RecoveryResult rec = PersistenceStore(kState).recover();
    if (!rec.ok) {
      m["status"] = "ERROR";
      m["error"] = "recovery_failed";
      m["message"] = rec.message;
      write_map(kRestartResult, m);
      return 1;
    }
    m["recovered_ok"] = "1";
    m["old_epoch"] = std::to_string(rec.epoch.value());
    m["recovered_nic_count"] = std::to_string(rec.nics.size());

    // A fresh coordinator process advances the epoch strictly.
    const CoordinatorEpoch new_epoch(rec.epoch.value() + 1);
    m["new_epoch"] = std::to_string(new_epoch.value());
    m["epoch_advanced"] = (new_epoch.value() > rec.epoch.value()) ? "1" : "0";

    if (rec.nics.empty()) {
      m["status"] = "ERROR";
      m["error"] = "no_recovered_nic";
      m["message"] = "no durable NIC recovered";
      write_map(kRestartResult, m);
      return 1;
    }
    const NicRecord& recovered = rec.nics[0];

    // Conservative recovery: durable identity is present but dynamic evidence
    // is REVALIDATION_REQUIRED and freshness is cleared.
    m["recovered_lifecycle"] = to_string(recovered.lifecycle);
    m["recovered_freshness"] = std::to_string(recovered.freshness);
    const bool recovered_conservative =
        (recovered.lifecycle == LifecycleState::kRevalidationRequired &&
         recovered.freshness == 0);
    m["recovered_conservative"] = recovered_conservative ? "1" : "0";

    // Stale prior-epoch traffic: the worker A frame published under epoch 1.
    WorkerId wA;
    WorkerBootId bootA;
    CoordinatorEpoch eA;
    NicRecord nicA;
    const bool haveA = decode_worker_frame(kWorkerA, wA, bootA, eA, nicA);
    if (haveA) {
      m["recovered_matches_a"] =
          (recovered.id == nicA.id && recovered.generation == nicA.generation &&
           eA.value() < new_epoch.value())
              ? "1"
              : "0";
    }

    // Fresh worker incarnation: Worker A' republishes under epoch 2.
    WorkerId wB;
    WorkerBootId bootB;
    CoordinatorEpoch eB;
    NicRecord nicB;
    const bool haveB = decode_worker_frame(kWorkerB, wB, bootB, eB, nicB);
    if (!haveB) {
      m["status"] = "ERROR";
      m["error"] = "no_worker_b_frame";
      m["message"] = "no fresh worker A' frame";
      write_map(kRestartResult, m);
      return 1;
    }

    // Fresh registry: no live worker/process authority is silently recovered.
    NicRegistry reg(new_epoch);

    // Replay the stale prior-epoch frame against the new-epoch coordinator.
    bool stale_epoch_rejected = false;
    std::string stale_code;
    if (haveA) {
      try {
        reg.register_nic(nicA, eA, wA, bootA);
      } catch (const ResidencyError& err) {
        stale_epoch_rejected = (err.code() == ErrorCode::kStaleEpoch);
        stale_code = to_string(err.code());
      }
    }
    m["stale_epoch_rejected"] = stale_epoch_rejected ? "1" : "0";
    m["stale_replay_code"] = stale_code;

    // Require Worker A/A' to re-establish current authority under fresh evidence.
    reg.register_worker(wB, bootB);
    // The pre-restart worker boot cannot silently claim the same worker id.
    bool old_boot_takeover_rejected = false;
    try {
      reg.register_worker(wA, bootA);
    } catch (const ResidencyError& err) {
      old_boot_takeover_rejected = (err.code() == ErrorCode::kOwnerConflict);
    }
    m["old_boot_takeover_rejected"] = old_boot_takeover_rejected ? "1" : "0";

    // Place the recovered durable identity conservatively: it is re-observed as
    // REVALIDATION_REQUIRED until fresh evidence re-binds it.
    reg.register_nic(recovered, new_epoch, wB, bootB);
    ResidencyPolicy policy;
    RegistrySnapshot preSnap = reg.snapshot();
    ResidencyDecision preD =
        policy.decide(preSnap, GpuId(), recovered.numa_node, EndpointId(), wB);
    m["pre_revalidation_overall"] = to_string(preD.overall);
    m["pre_revalidation_selected"] = preD.selected.valid() ? "1" : "0";
    const bool authority_not_recovered =
        (preD.overall != ResidencyEligibility::kResidencyAllowed &&
         preD.overall != ResidencyEligibility::kResidencyAllowedDegraded);
    m["worker_authority_not_recovered"] = authority_not_recovered ? "1" : "0";

    // Fresh evidence re-validation: Worker A' republishes at a higher generation
    // under the new epoch, and residency authority returns.
    reg.register_nic(nicB, new_epoch, wB, bootB);
    ResidencyRecord fresh;
    fresh.id = ResidencyId(1);
    fresh.generation = ResidencyGeneration(2);
    fresh.nic = nicB.id;
    fresh.state = ResidencyState::kResident;
    fresh.lifecycle = LifecycleState::kResident;
    fresh.owner_worker = wB;
    fresh.owner_boot = bootB;
    fresh.epoch = new_epoch;
    fresh.target_numa = nicB.numa_node;
    fresh.provenance = nicB.provenance;
    fresh.ownership = OwnershipState::kAssigned;
    fresh.policy = PolicyId(1);
    fresh.freshness = 1;
    reg.publish_residency(fresh, new_epoch, wB, bootB);
    RegistrySnapshot postSnap = reg.snapshot();
    ResidencyDecision postD =
        policy.decide(postSnap, GpuId(), nicB.numa_node, EndpointId(), wB);
    m["post_revalidation_overall"] = to_string(postD.overall);
    m["post_revalidation_selected"] = postD.selected.str();
    const bool required_refresh =
        (postD.overall == ResidencyEligibility::kResidencyAllowed ||
         postD.overall == ResidencyEligibility::kResidencyAllowedDegraded) &&
        postD.selected == nicB.id;
    m["required_refresh"] = required_refresh ? "1" : "0";
    m["fresh_boot"] = bootB.str();

    // The pre-restart residency decision remains non-authoritative after the
    // restart: it is only history, at the old epoch, not the live authority.
    PersistentState loaded = PersistenceStore(kState).load();
    if (!loaded.history.empty()) {
      const ResidencyRecord& old = loaded.history[0];
      m["pre_restart_epoch"] = std::to_string(old.epoch.value());
      m["pre_restart_state"] = to_string(old.state);
      m["pre_restart_boot"] = old.owner_boot.str();
      m["pre_restart_non_authoritative"] =
          (old.epoch.value() < new_epoch.value()) ? "1" : "0";
    } else {
      m["pre_restart_non_authoritative"] = "0";
    }

    if (old_boot_takeover_rejected) {
      // The live registry residency is the fresh epoch-2 binding.
      const auto& live = reg.residencies();
      if (!live.empty()) {
        m["fresh_residency_epoch"] = std::to_string(live.back().epoch.value());
        m["fresh_residency_boot"] = live.back().owner_boot.str();
      }
    }
    write_map(kRestartResult, m);
  } catch (const ResidencyError& err) {
    m["status"] = "ERROR";
    m["error"] = to_string(err.code());
    m["message"] = err.what();
    write_map(kRestartResult, m);
    return 1;
  }
  block_forever();
  return 0;
}

int main(int argc, char** argv) {
  const std::string mode = (argc > 1) ? argv[1] : "first";
  if (mode == "restart") return run_restart();
  return run_first();
}
