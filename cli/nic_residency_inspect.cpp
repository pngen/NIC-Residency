// nic_residency_inspect: inspection + validation CLI for NIC Residency.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nicresidency/engine.hpp"
#include "nicresidency/policy.hpp"
#include "nicresidency/persistence.hpp"
#include "nicresidency/synthetic_backend.hpp"
#include "nicresidency/system_backend.hpp"
#include "nicresidency/version.hpp"

using namespace nicresidency;

static void usage() {
  std::printf("nic_residency_inspect [options]\n");
  std::printf("  --system                 run REAL host discovery (Windows)\n");
  std::printf("  --synthetic <scenario>   run a synthetic scenario\n");
  std::printf("  --query --gpu <id> [--numa <id>]  resolve the best resident NIC\n");
  std::printf("  --json                    emit deterministic JSON\n");
  std::printf("  --version                 print version\n");
  std::printf("  --help                    this help\n");
  std::printf("  scenarios: one_gpu two_numa root_complex pcie_switch remote_numa smartnic dpu pf_vfs dense unsupported\n");
}

static SyntheticScenario scene_by_name(const std::string& s) {
  if (s == "one_gpu") return SyntheticScenario::kOneNicLocalToOneGpu;
  if (s == "two_numa") return SyntheticScenario::kTwoNicsDifferentNuma;
  if (s == "root_complex") return SyntheticScenario::kSameRootComplexPreference;
  if (s == "pcie_switch") return SyntheticScenario::kSamePcieSwitchPreference;
  if (s == "remote_numa") return SyntheticScenario::kRemoteNumaFallback;
  if (s == "smartnic") return SyntheticScenario::kSmartNicLocalMemory;
  if (s == "dpu") return SyntheticScenario::kDpuLikeLocalMemory;
  if (s == "pf_vfs") return SyntheticScenario::kPfWithMultipleVfs;
  if (s == "dense") return SyntheticScenario::kDenseCandidateSet;
  if (s == "unsupported") return SyntheticScenario::kUnsupportedCapability;
  return SyntheticScenario::kOneNicLocalToOneGpu;
}

static std::string json_escape(const std::string& s) {
  std::string o; o.reserve(s.size());
  for (char c : s) { unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x20 && c != '"' && c != '\\') o.push_back(c); else o.push_back('_'); }
  return o;
}

static void print_nics(const RegistrySnapshot& snap, bool json) {
  if (json) {
    std::printf("\"nics\":[\n");
    bool first = true;
    for (const auto& kv : snap.nics()) {
      const NicRecord& n = kv.second;
      if (!first) std::printf(",\n"); first = false;
      std::printf("{\"id\":\"%s\",\"gen\":%llu,\"os\":\"%s\",\"pci\":\"%s\",\"kind\":\"%s\",\"numa\":%llu,\"link\":\"%s\",\"prov\":\"%s\",\"lifecycle\":\"%s\"}"
               , n.id.str().c_str(), (unsigned long long)n.generation.value(), json_escape(n.os_name).c_str(),
               n.pci.str().c_str(), to_string(n.device_kind), (unsigned long long)n.numa_node.value(),
               to_string(n.link_state), n.provenance.str().c_str(), to_string(n.lifecycle));
    }
    std::printf("\n]\n");
  } else {
    std::printf("NICs (%zu):\n", snap.nic_count());
    for (const auto& kv : snap.nics()) {
      const NicRecord& n = kv.second;
      std::printf("  id=%s gen=%llu os=%s pci=%s kind=%s numa=%llu link=%s prov=%s\n",
                 n.id.str().c_str(), (unsigned long long)n.generation.value(), n.os_name.c_str(),
                 n.pci.str().c_str(), to_string(n.device_kind), (unsigned long long)n.numa_node.value(),
                 to_string(n.link_state), n.provenance.str().c_str());
    }
  }
}

static void print_gpus(const RegistrySnapshot& snap, bool json) {
  if (json) { std::printf("\"gpus\":[\n"); bool first = true;
    for (const auto& kv : snap.gpus()) { const GpuRecord& g = kv.second;
      if (!first) std::printf(",\n"); first = false;
      std::printf("{\"id\":\"%s\",\"name\":\"%s\",\"pci\":\"%s\",\"numa\":%llu,\"cuda\":%llu,\"prov\":\"%s\"}"
        , g.id.str().c_str(), json_escape(g.name).c_str(), g.pci.str().c_str(),
          (unsigned long long)g.numa_node.value(), (unsigned long long)g.cuda_device.value(), g.provenance.str().c_str());
    }
    std::printf("\n]\n");
  } else {
    std::printf("Accelerators (%zu):\n", snap.gpu_count());
    for (const auto& kv : snap.gpus()) { const GpuRecord& g = kv.second;
      std::printf("  id=%s name=%s pci=%s numa=%llu cuda=%llu prov=%s\n", g.id.str().c_str(),
        g.name.c_str(), g.pci.str().c_str(), (unsigned long long)g.numa_node.value(),
        (unsigned long long)g.cuda_device.value(), g.provenance.str().c_str());
    }
  }
}

static void run_system(bool json) {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  DiscoveryContext ctx; ctx.freshness = 1;
  try {
    SystemBackend sb;
    DiscoveryResult d = sb.discover(ctx);
    eng.ingest(d, w, boot);
  } catch (const ResidencyError& e) {
    if (json) std::printf("\"system_error\":\"%s\"\n", json_escape(e.what()).c_str());
    else std::printf("System discovery: %s\n", e.what());
  }
  try {
    if (NvidiaAffinityBackend::available()) {
      NvidiaAffinityBackend nb;
      DiscoveryResult d = nb.discover(ctx);
      eng.ingest(d, w, boot);
    } else if (json) { std::printf("\"nvidia\":\"UNSUPPORTED\"\n"); }
    else std::printf("NVIDIA/NVML: UNSUPPORTED (no NVML)\n");
  } catch (const ResidencyError& e) {
    if (json) std::printf("\"nvidia_error\":\"%s\"\n", json_escape(e.what()).c_str());
    else std::printf("NVIDIA: %s\n", e.what());
  }
  RegistrySnapshot snap = eng.registry().snapshot();
  if (json) { std::printf("{\n");
    std::printf("\"host_epoch\":\"%s\",\n", snap.epoch().str().c_str());
    print_nics(snap, true); std::printf(",\n");
    print_gpus(snap, true); std::printf(",\n");
    if (snap.topology()) std::printf("\"topo_nodes\":%zu,\"topo_gen\":%s,\n", snap.topology()->node_count(), snap.topology()->generation().str().c_str());
    std::printf("\"summary\":\"%s\"\n}\n", json_escape(snap.summary()).c_str());
  } else {
    std::printf("Epoch=%s\n", snap.epoch().str().c_str());
    print_nics(snap, false);
    print_gpus(snap, false);
    if (snap.topology()) std::printf("Topology nodes=%zu gen=%s\n", snap.topology()->node_count(), snap.topology()->generation().str().c_str());
    std::printf("%s\n", snap.summary().c_str());
  }
}

static void run_synthetic(const std::string& name, bool json) {
  ResidencyEngine eng(CoordinatorEpoch(1));
  WorkerId w(1);
  WorkerBootId boot = eng.register_host_worker(w);
  SyntheticBackend sb(scene_by_name(name));
  DiscoveryContext ctx; ctx.freshness = 1;
  DiscoveryResult d = sb.discover(ctx);
  eng.ingest(d, w, boot);
  RegistrySnapshot snap = eng.registry().snapshot();
  if (json) std::printf("{\"scenario\":\"%s\",", name.c_str());
  else std::printf("Scenario: %s\n", name.c_str());
  if (json) { print_nics(snap, true); std::printf(",\n"); print_gpus(snap, true); std::printf(",\n\"summary\":\"%s\"}\n", json_escape(snap.summary()).c_str()); }
  else { print_nics(snap, false); print_gpus(snap, false); std::printf("%s\n", snap.summary().c_str()); }
}

static void run_query(const RegistrySnapshot& snap, GpuId gpu, NumaNodeId numa, bool json) {
  ResidencyPolicy policy;
  policy.set_allow_synthetic(true);
  ResidencyDecision dec = policy.decide(snap, gpu, numa, EndpointId(), WorkerId(1));
  if (json) {
    std::printf("{\"overall\":\"%s\",\"selected\":\"%s\",\"explanation\":\"%s\",\"topo_gen\":%s,\"epoch\":\"%s\"}\n",
      to_string(dec.overall), dec.selected.valid() ? dec.selected.str().c_str() : "NONE",
      json_escape(dec.explanation).c_str(), dec.topology_generation.str().c_str(), snap.epoch().str().c_str());
  } else {
    std::printf("Overall=%s\n", to_string(dec.overall));
    std::printf("Selected=%s\n", dec.selected.valid() ? dec.selected.str().c_str() : "NONE");
    std::printf("Explanation: %s\n", dec.explanation.c_str());
    for (const auto& c : dec.evaluated) {
      std::printf("  candidate=%s elig=%s score=%lld\n", c.nic.str().c_str(), to_string(c.eligibility), (long long)c.rank_score);
      for (const auto& r : c.rejections) std::printf("      reject: %s  (%s)\n", r.name.c_str(), r.detail.c_str());
    }
  }
}

int main(int argc, char** argv) {
  bool json = false;
  std::string mode = "system";
  std::string scenario;
  GpuId gpu;  NumaNodeId numa;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json") json = true;
    else if (a == "--system") mode = "system";
    else if (a == "--synthetic") { mode = "synthetic"; if (i+1 < argc) scenario = argv[++i]; }
    else if (a == "--query") mode = "query";
    else if (a == "--gpu" && i+1 < argc) gpu = GpuId(std::strtoull(argv[++i], nullptr, 0));
    else if (a == "--numa" && i+1 < argc) numa = NumaNodeId(std::strtoull(argv[++i], nullptr, 0));
    else if (a == "--version") { std::printf("%s %s (%s)\n", kProductName, kVersionString, kVendor); return 0; }
    else if (a == "--help") { usage(); return 0; }
  }
  if (mode == "system") { run_system(json); return 0; }
  if (mode == "synthetic") { run_synthetic(scenario, json); return 0; }
  if (mode == "query") {
    ResidencyEngine eng(CoordinatorEpoch(1)); WorkerId w(1); WorkerBootId boot = eng.register_host_worker(w);
    SyntheticBackend sb(SyntheticScenario::kTwoNicsDifferentNuma); DiscoveryContext ctx; ctx.freshness = 1;
    DiscoveryResult d = sb.discover(ctx); eng.ingest(d, w, boot);
    RegistrySnapshot snap = eng.registry().snapshot();
    if (!gpu.valid()) gpu = GpuId(101);
    run_query(snap, gpu, numa, json);
    return 0;
  }
  usage();
  return 1;
}
