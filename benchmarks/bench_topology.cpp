// bench_topology.cpp
// NIC Residency benchmark: locality queries over a large SYNTHETIC topology
// (root complexes -> PCIe switches -> devices) built via TopologyBuilder.
//
// NOTE: this is a synthetic fixture, not hardware performance measurement.
//
// SPDX-License-Identifier: Apache-2.0
#include <nicresidency/enums.hpp>
#include <nicresidency/locality.hpp>
#include <nicresidency/provenance.hpp>
#include <nicresidency/topology.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace nicresidency;

namespace {

Provenance syn() {
  return Provenance(EvidenceKind::kSynthetic, {Source::kSyntheticFixture});
}

}  // namespace

int main() {
  // Layout: 2 root complexes, NRC switches each, P device nodes per switch.
  constexpr int kSwitchPerRc = 12;      // 24 switches total
  constexpr int kDevPerSwitch = 40;     // 960 device nodes
  constexpr std::size_t kQueryIters = 100000;

  TopologyBuilder builder;
  const PciNodeId rc0(1), rc1(2);
  builder.add_node(TopologyNode(rc0, PciAddress(0, 0x00, 0, 0),
                                TopologyNodeKind::kRootComplex, PciNodeId(),
                                NumaNodeId(0), syn()));
  builder.add_node(TopologyNode(rc1, PciAddress(0, 0x80, 0, 0),
                                TopologyNodeKind::kRootComplex, PciNodeId(),
                                NumaNodeId(1), syn()));

  std::vector<PciAddress> devices;
  devices.reserve(static_cast<std::size_t>(kSwitchPerRc * kDevPerSwitch * 2));

  std::uint64_t dev_counter = 0;
  auto add_switch = [&](std::uint64_t sw_id, PciNodeId rc, std::uint16_t bus,
                        NumaNodeId numa) {
    builder.add_node(TopologyNode(PciNodeId(sw_id), PciAddress(0, static_cast<std::uint8_t>(bus), 0, 0),
                                  TopologyNodeKind::kSwitch, rc, numa, syn()));
    builder.add_edge(PciNodeId(sw_id), rc);
    for (int d = 1; d <= kDevPerSwitch; ++d) {
      const std::uint64_t node_id = 100000 + dev_counter++;
      const PciAddress pci(0, static_cast<std::uint8_t>(bus), static_cast<std::uint8_t>(d), 0);
      builder.add_node(TopologyNode(PciNodeId(node_id), pci,
                                    TopologyNodeKind::kDevice,
                                    PciNodeId(sw_id), numa, syn()));
      builder.add_edge(PciNodeId(node_id), PciNodeId(sw_id));
      devices.push_back(pci);
    }
  };

  for (int s = 0; s < kSwitchPerRc; ++s) {
    add_switch(100 + static_cast<std::uint64_t>(s), rc0,
               static_cast<std::uint16_t>(0x01 + s), NumaNodeId(0));
    add_switch(200 + static_cast<std::uint64_t>(s), rc1,
               static_cast<std::uint16_t>(0x81 + s), NumaNodeId(1));
  }

  const TopologySnapshot topo =
      builder.build(TopologyGeneration(1));
  const std::size_t topo_nodes = topo.node_count();

  // Pre-compute a stable mix of query pairs (same/different switch, cross-RC).
  const std::size_t m = devices.size();
  std::vector<std::pair<PciAddress, PciAddress>> pairs;
  pairs.reserve(4096);
  for (std::size_t i = 0; i < 4096; ++i) {
    const std::size_t a = i % m;
    const std::size_t b = (i * 7 + 3) % m;
    pairs.emplace_back(devices[a], devices[b]);
  }

  // --- locality query throughput (measured completed work) ------------------
  std::int64_t checksum = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < kQueryIters; ++i) {
    const auto& pr = pairs[i % pairs.size()];
    Locality loc = topo.locality_between(pr.first, pr.second);
    // Force use of the result so the call cannot be optimized out.
    checksum += loc.factors().cost;
    checksum += static_cast<std::int64_t>(loc.relationship());
  }
  auto t1 = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const double ns = ms * 1e6 / static_cast<double>(kQueryIters);

  std::printf(
      "bench_topology: %zu iterations in %.3f ms (%.1f ns/op), "
      "context: nics=0 funcs=0 queues=0 attachments=0 candidates=0 "
      "topo_nodes=%zu (synthetic) checksum=%lld\n",
      kQueryIters, ms, ns, topo_nodes,
      static_cast<long long>(checksum));
  return 0;
}
