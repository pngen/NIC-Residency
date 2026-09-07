
// SyntheticBackend: deterministic fixture scenarios, always labeled SYNTHETIC.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/synthetic_backend.hpp"

#include <utility>
#include <vector>

namespace nicresidency {

namespace {

PciNodeId pnid(std::uint64_t v) { return PciNodeId(v); }
NicId nid(std::uint64_t v) { return NicId(v); }
FunctionId fid(std::uint64_t v) { return FunctionId(v); }
PortId pid(std::uint64_t v) { return PortId(v); }
QueueId qid(std::uint64_t v) { return QueueId(v); }
GpuId gid(std::uint64_t v) { return GpuId(v); }
// NOTE: NumaNodeId(0) is the null/invalid id, so produce 1-based valid ids.
NumaNodeId numa(std::uint64_t v) { return NumaNodeId(v + 1); }

Provenance syn() {
  return Provenance(EvidenceKind::kSynthetic, {Source::kSyntheticFixture});
}

NicRecord make_nic(std::uint64_t id, std::string name, PciAddress pci, NumaNodeId n,
                   const DiscoveryContext& ctx, bool rdma, bool local_mem) {
  NicRecord nic;
  nic.id = nid(id);
  nic.generation = NicGeneration(1);
  nic.os_name = std::move(name);
  nic.os_description = "synthetic NIC fixture";
  nic.pci = pci;
  nic.vendor_id = 0x1DB6;
  nic.device_id = 0x1001;
  nic.device_kind = DeviceKind::kNic;
  nic.function_class = FunctionClass::kPhysicalFunction;
  nic.capability.set_rdma_capable(rdma);
  nic.capability.set_offload_capable(true);
  nic.capability.set_local_memory_observable(local_mem);
  nic.capability.set_accelerator_affinity_supported(true);
  nic.capability.set_generation(CapabilityGeneration(1));
  nic.capability.set_provenance(syn());
  nic.numa_node = n;
  nic.link_state = LinkState::kUp;
  nic.health = HealthState::kReady;
  nic.lifecycle = LifecycleState::kAvailable;
  nic.provenance = syn();
  nic.freshness = ctx.freshness;
  return nic;
}

PortRecord make_port(std::uint64_t id, NicId nic, FunctionId func, const DiscoveryContext& ctx) {
  PortRecord p;
  p.id = pid(id);
  p.generation = PortGeneration(1);
  p.parent_nic = nic;
  p.parent_function = func;
  p.os_name = "synth-port" + std::to_string(id);
  p.link_state = LinkState::kUp;
  p.link_speed_mbps = 100000;
  p.lifecycle = LifecycleState::kAvailable;
  p.provenance = syn();
  p.freshness = ctx.freshness;
  return p;
}

QueueRecord make_queue(std::uint64_t id, FunctionId func, NicId nic, std::string kind,
                       const DiscoveryContext& ctx) {
  QueueRecord q;
  q.id = qid(id);
  q.generation = QueueGeneration(1);
  q.parent_function = func;
  q.parent_nic = nic;
  q.kind = std::move(kind);
  q.lifecycle = LifecycleState::kAvailable;
  q.provenance = syn();
  q.freshness = ctx.freshness;
  return q;
}

TopologySnapshot build_topology(TopologyGeneration gen, std::vector<TopologyNode> nodes,
                                std::vector<std::pair<PciNodeId, PciNodeId>> edges) {
  TopologyBuilder b;
  for (auto& n : nodes) b.add_node(n);
  for (auto& e : edges) b.add_edge(e.first, e.second);
  return b.build(gen);
}

struct TopoShape {
  PciNodeId rc0, rc1, sw0, sw1, sw2, sw3;
};

}  // namespace

SyntheticBackend::SyntheticBackend() = default;
SyntheticBackend::SyntheticBackend(SyntheticScenario scenario) : scenario_(scenario) {}

DiscoveryResult SyntheticBackend::discover(const DiscoveryContext& ctx) const {
  DiscoveryResult out;
  out.notes.push_back(std::string("synthetic fixture scenario: ") + to_string(scenario_));

  TopoShape t;
  t.rc0 = pnid(1); t.rc1 = pnid(2);
  t.sw0 = pnid(101); t.sw1 = pnid(102); t.sw2 = pnid(103); t.sw3 = pnid(104);

  std::vector<TopologyNode> nodes = {
      TopologyNode(t.rc0, PciAddress(0, 0x00, 0, 0), TopologyNodeKind::kRootComplex, PciNodeId(), numa(0), syn()),
      TopologyNode(t.rc1, PciAddress(0, 0x80, 0, 0), TopologyNodeKind::kRootComplex, PciNodeId(), numa(1), syn()),
      TopologyNode(t.sw0, PciAddress(0, 0x01, 0, 0), TopologyNodeKind::kSwitch, t.rc0, numa(0), syn()),
      TopologyNode(t.sw1, PciAddress(0, 0x02, 0, 0), TopologyNodeKind::kSwitch, t.rc0, numa(0), syn()),
      TopologyNode(t.sw2, PciAddress(0, 0x81, 0, 0), TopologyNodeKind::kSwitch, t.rc1, numa(1), syn()),
      TopologyNode(t.sw3, PciAddress(0, 0x82, 0, 0), TopologyNodeKind::kSwitch, t.rc1, numa(1), syn()),
  };
  std::vector<std::pair<PciNodeId, PciNodeId>> edges = {
      {t.sw0, t.rc0}, {t.sw2, t.rc1},
  };

  auto add_dev_node = [&](PciNodeId id, PciAddress pci, PciNodeId parent, NumaNodeId n) {
    nodes.push_back(TopologyNode(id, pci, TopologyNodeKind::kDevice, parent, n, syn()));
    edges.push_back({id, parent});
  };

  auto add_gpu = [&](std::uint64_t id, PciAddress pci, NumaNodeId n) {
    GpuRecord gpu;
    gpu.id = gid(id);
    gpu.generation = GpuGeneration(1);
    gpu.name = "synth-gpu" + std::to_string(id);
    gpu.pci = pci;
    gpu.cuda_device = DeviceId(0);
    gpu.cuda_verified = true;
    gpu.numa_node = n;
    gpu.provenance = syn();
    gpu.freshness = ctx.freshness;
    out.gpus.push_back(gpu);
  };

  auto finish = [&]() {
    out.topology = std::make_unique<TopologySnapshot>(
        build_topology(ctx.topology_generation, std::move(nodes), std::move(edges)));
    return std::move(out);
  };

  switch (scenario_) {
    case SyntheticScenario::kOneNicLocalToOneGpu: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      add_dev_node(pnid(1001), PciAddress(0, 0x01, 0, 1), t.sw0, numa(0));
      NicRecord nic = make_nic(10, "synth-nic0", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, false);
      out.nics.push_back(nic);
      FunctionRecord fn;
      fn.id = fid(10);
      fn.generation = FunctionGeneration(1);
      fn.parent_nic = nic.id;
      fn.function_class = FunctionClass::kPhysicalFunction;
      fn.numa_node = numa(0);
      fn.capability = nic.capability;
      fn.lifecycle = LifecycleState::kAvailable;
      fn.provenance = syn();
      fn.freshness = ctx.freshness;
      out.functions.push_back(fn);
      out.ports.push_back(make_port(10, nic.id, fn.id, ctx));
      out.queues.push_back(make_queue(10, fn.id, nic.id, "RX", ctx));
      add_gpu(100, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kTwoNicsDifferentNuma: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      add_dev_node(pnid(1001), PciAddress(0, 0x81, 0, 0), t.sw2, numa(1));
      out.nics.push_back(make_nic(20, "synth-nic-numa0", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, false));
      out.nics.push_back(make_nic(21, "synth-nic-numa1", PciAddress(0, 0x81, 0, 0), numa(1), ctx, true, false));
      add_gpu(101, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kSameRootComplexPreference:
    case SyntheticScenario::kSamePcieSwitchPreference: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      add_dev_node(pnid(1001), PciAddress(0, 0x02, 0, 0), t.sw1, numa(0));
      add_dev_node(pnid(1002), PciAddress(0, 0x01, 0, 1), t.sw0, numa(0));
      out.nics.push_back(make_nic(30, "synth-nic-sw0", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, false));
      out.nics.push_back(make_nic(31, "synth-nic-sw1", PciAddress(0, 0x02, 0, 0), numa(0), ctx, true, false));
      add_gpu(102, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kRemoteNumaFallback: {
      add_dev_node(pnid(1000), PciAddress(0, 0x81, 0, 0), t.sw2, numa(1));
      add_dev_node(pnid(1001), PciAddress(0, 0x01, 0, 1), t.sw0, numa(0));
      out.nics.push_back(make_nic(40, "synth-nic-remote", PciAddress(0, 0x81, 0, 0), numa(1), ctx, true, false));
      add_gpu(103, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kSmartNicLocalMemory: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      NicRecord nic = make_nic(50, "synth-smartnic", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, true);
      nic.device_kind = DeviceKind::kSmartNic;
      nic.capability.set_offload_capable(true);
      out.nics.push_back(nic);
      add_gpu(104, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kDpuLikeLocalMemory: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      NicRecord nic = make_nic(60, "synth-dpu", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, true);
      nic.device_kind = DeviceKind::kDpu;
      nic.capability.set_offload_capable(true);
      out.nics.push_back(nic);
      add_gpu(105, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kPfWithMultipleVfs: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      NicRecord nic = make_nic(70, "synth-pf", PciAddress(0, 0x01, 0, 0), numa(0), ctx, true, false);
      nic.function_class = FunctionClass::kPhysicalFunction;
      nic.capability.set_sr_iov_capable(true);
      out.nics.push_back(nic);
      const std::uint64_t base = 700;
      for (int i = 0; i < 4; ++i) {
        FunctionRecord fr;
        fr.id = fid(base + static_cast<std::uint64_t>(i));
        fr.generation = FunctionGeneration(1);
        fr.parent_nic = nic.id;
        fr.function_class = FunctionClass::kVirtualFunction;
        fr.numa_node = numa(0);
        fr.lifecycle = LifecycleState::kAvailable;
        fr.provenance = syn();
        fr.freshness = ctx.freshness;
        out.functions.push_back(fr);
        out.ports.push_back(make_port(base + static_cast<std::uint64_t>(i), nic.id, fr.id, ctx));
        out.queues.push_back(make_queue(base + static_cast<std::uint64_t>(i), fr.id, nic.id, "RDMA", ctx));
      }
      add_gpu(106, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kDenseCandidateSet: {
      for (std::uint64_t i = 0; i < 200; ++i) {
        const std::uint64_t bus = (i % 2 == 0) ? 0x01 : 0x81;
        const PciNodeId parent = (i % 2 == 0) ? t.sw0 : t.sw2;
        const NumaNodeId n = (i % 2 == 0) ? numa(0) : numa(1);
        const std::uint8_t dev = static_cast<std::uint8_t>((i / 2) & 0x1F);
        const std::uint8_t fn = static_cast<std::uint8_t>(i % 8);
        add_dev_node(pnid(2000 + i), PciAddress(0, static_cast<std::uint8_t>(bus), dev, fn), parent, n);
        out.nics.push_back(make_nic(200 + i, "synth-dense", PciAddress(0, static_cast<std::uint8_t>(bus), dev, fn),
                                    n, ctx, true, false));
      }
      add_gpu(107, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
    case SyntheticScenario::kUnsupportedCapability: {
      add_dev_node(pnid(1000), PciAddress(0, 0x01, 0, 0), t.sw0, numa(0));
      NicRecord nic = make_nic(80, "synth-unsupported", PciAddress(0, 0x01, 0, 0), numa(0), ctx, false, false);
      nic.capability.set_provenance(Provenance(EvidenceKind::kUnsupported, {Source::kPci}));
      out.nics.push_back(nic);
      add_gpu(108, PciAddress(0, 0x01, 0, 1), numa(0));
      return finish();
    }
  }
  return out;
}

}  // namespace nicresidency
