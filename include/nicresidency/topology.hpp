#pragma once
// Topology model: a bounded, validated graph of PCI / NUMA locality evidence.
//
// Topology never *claims* a relationship it cannot prove.  All locality facts
// derived here are labeled REAL (observed hierarchy), DERIVED (computed from
// real hierarchy), or UNKNOWN (insufficient evidence).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "nicresidency/error.hpp"
#include "nicresidency/id.hpp"
#include "nicresidency/locality.hpp"
#include "nicresidency/provenance.hpp"

namespace nicresidency {

enum class TopologyNodeKind {
  kRootComplex,
  kSwitch,
  kBridge,
  kDevice,
  kNumaDomain,
  kUnknown,
};

inline const char* to_string(TopologyNodeKind k) noexcept {
  switch (k) {
    case TopologyNodeKind::kRootComplex: return "ROOT_COMPLEX";
    case TopologyNodeKind::kSwitch: return "SWITCH";
    case TopologyNodeKind::kBridge: return "BRIDGE";
    case TopologyNodeKind::kDevice: return "DEVICE";
    case TopologyNodeKind::kNumaDomain: return "NUMA_DOMAIN";
    case TopologyNodeKind::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

class TopologyNode {
 public:
  TopologyNode() = default;
  TopologyNode(PciNodeId id, PciAddress pci, TopologyNodeKind kind, PciNodeId parent,
               NumaNodeId numa, Provenance provenance)
      : id_(id), pci_(pci), kind_(kind), parent_(parent), numa_(numa),
        provenance_(std::move(provenance)) {}

  PciNodeId id() const noexcept { return id_; }
  const PciAddress& pci() const noexcept { return pci_; }
  TopologyNodeKind kind() const noexcept { return kind_; }
  PciNodeId parent() const noexcept { return parent_; }
  NumaNodeId numa_node() const noexcept { return numa_; }
  const Provenance& provenance() const noexcept { return provenance_; }

 private:
  PciNodeId id_;
  PciAddress pci_;
  TopologyNodeKind kind_{TopologyNodeKind::kUnknown};
  PciNodeId parent_;
  NumaNodeId numa_;
  Provenance provenance_;
};

// Bounded topology snapshot.  Immutable by convention: once built, it is read
// by registry/decision paths under a stable generation.
class TopologySnapshot {
 public:
  TopologySnapshot() = default;
  explicit TopologySnapshot(TopologyGeneration generation) : generation_(generation) {}

  TopologyGeneration generation() const noexcept { return generation_; }
  void set_generation(TopologyGeneration g) noexcept { generation_ = g; }

  std::size_t node_count() const noexcept { return nodes_.size(); }
  std::size_t edge_count() const noexcept { return edges_.size(); }

  const TopologyNode* node(PciNodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
  }
  // Look up the node for a specific PCI address (first match).
  const TopologyNode* node_for_pci(const PciAddress& pci) const {
    for (const auto& kv : nodes_) {
      if (kv.second.pci() == pci) return &kv.second;
    }
    return nullptr;
  }

  const std::map<PciNodeId, TopologyNode>& nodes() const noexcept { return nodes_; }

  bool has_edge(PciNodeId a, PciNodeId b) const {
    auto key = std::make_pair(a, b);
    return edges_.find(key) != edges_.end() ||
           edges_.find(std::make_pair(b, a)) != edges_.end();
  }

  // --- locality queries ------------------------------------------------------

  bool same_root_complex(const PciAddress& a, const PciAddress& b) const {
    const auto* na = node_for_pci(a);
    const auto* nb = node_for_pci(b);
    if (!na || !nb) return false;
    PciNodeId ra = root_complex_of(na->id());
    PciNodeId rb = root_complex_of(nb->id());
    return ra.valid() && ra == rb;
  }

  bool same_pcie_switch(const PciAddress& a, const PciAddress& b) const {
    const auto* na = node_for_pci(a);
    const auto* nb = node_for_pci(b);
    if (!na || !nb) return false;
    PciNodeId sa = switch_of(na->id());
    PciNodeId sb = switch_of(nb->id());
    return sa.valid() && sa == sb;
  }

  bool same_numa_node(const PciAddress& a, const PciAddress& b) const {
    const auto* na = node_for_pci(a);
    const auto* nb = node_for_pci(b);
    if (!na || !nb) return false;
    NumaNodeId nna = na->numa_node();
    NumaNodeId nnb = nb->numa_node();
    return nna.valid() && nna == nnb;
  }

  // Full locality statement between two PCI addresses, labeled with evidence.
  Locality locality_between(const PciAddress& a, const PciAddress& b) const {
    const TopologyNode* na = node_for_pci(a);
    const TopologyNode* nb = node_for_pci(b);
    Provenance prov(EvidenceKind::kUnsupported);

    if (na && nb) {
      // Preserve the evidence class of the underlying hierarchy.  Both REAL
      // yields kReal; both SYNTHETIC yields kSynthetic; any DERIVED/heuristic
      // grouping keeps kind kReal but records an explicit Source::kDerived so
      // the explanation can state the fact was DERIVED, not directly observed.
      bool both_real = na->provenance().kind() == EvidenceKind::kReal &&
                       nb->provenance().kind() == EvidenceKind::kReal;
      bool both_synth = na->provenance().kind() == EvidenceKind::kSynthetic &&
                        nb->provenance().kind() == EvidenceKind::kSynthetic;
      EvidenceKind kind = EvidenceKind::kReal;
      if (both_synth) kind = EvidenceKind::kSynthetic;
      std::vector<Source> srcs{Source::kPci, Source::kNuma};
      if (!both_real && !both_synth) srcs.push_back(Source::kDerived);
      prov = Provenance(kind, std::move(srcs));
    } else if (na || nb) {
      prov = Provenance(EvidenceKind::kReal, {Source::kPci, Source::kDerived});
    } else {
      // Neither node known: derive from the raw BDF comparison only, labeled
      // DERIVED — never treated as REAL topology.
      prov = Provenance(EvidenceKind::kReal, {Source::kPci, Source::kDerived});
    }

    LocalityFactors factors;
    if (na && nb) {
      factors.root_complex_same = same_root_complex(a, b);
      factors.pcie_switch_same = same_pcie_switch(a, b);
      factors.numa_same = same_numa_node(a, b);
      factors.cost = derivable_cost(na, nb, factors);
    } else {
      factors.cost = FastCost(a, b);
    }
    if (a == b) factors.direct_pci_neighbor = true;

    LocalityRelationship rel = classify(na, nb, a, b, factors);
    NumaNodeId numa;
    if (na) numa = na->numa_node();
    if (nb && !numa.valid()) numa = nb->numa_node();

    return Locality(rel, numa, b, prov, factors);
  }

  // Deterministic scalar cost: lower is better.  Only consult when the named
  // factors are present; never present this as the whole locality model.
  std::int64_t cost_between(const PciAddress& a, const PciAddress& b) const {
    const auto* na = node_for_pci(a);
    const auto* nb = node_for_pci(b);
    if (!na || !nb) return FastCost(a, b);
    LocalityFactors f;
    f.root_complex_same = same_root_complex(a, b);
    f.pcie_switch_same = same_pcie_switch(a, b);
    f.numa_same = same_numa_node(a, b);
    return derivable_cost(na, nb, f);
  }

 private:
  void finalize() {
    // Compute the nearest root-complex and switch ancestor for every node by
    // walking parent links.  Runs once at build; keeps queries O(1).
    for (const auto& kv : nodes_) {
      const PciNodeId id = kv.first;
      PciNodeId rc;
      PciNodeId sw;
      PciNodeId cur = id;
      while (cur.valid()) {
        auto it = nodes_.find(cur);
        if (it == nodes_.end()) break;
        if (it->second.kind() == TopologyNodeKind::kRootComplex) rc = cur;
        if (it->second.kind() == TopologyNodeKind::kSwitch) sw = cur;
        cur = it->second.parent();
      }
      root_complex_[id] = rc;
      switch_[id] = sw;
    }
  }

  PciNodeId root_complex_of(PciNodeId n) const {
    auto it = root_complex_.find(n);
    return it == root_complex_.end() ? PciNodeId{} : it->second;
  }
  PciNodeId switch_of(PciNodeId n) const {
    auto it = switch_.find(n);
    return it == switch_.end() ? PciNodeId{} : it->second;
  }

  static std::int64_t FastCost(const PciAddress& a, const PciAddress& b) {
    return a.distance_to(b);
  }

  static std::int64_t derivable_cost(const TopologyNode* na, const TopologyNode* nb,
                                     const LocalityFactors& f) {
    std::int64_t c = 0;
    if (f.numa_same) c += 0;
    else c += 1000;  // remote NUMA penalty
    if (f.root_complex_same) c += 0;
    else c += 200;  // different root complex
    if (f.pcie_switch_same) c -= 10;  // shared switch is better than root complex only
    // Prefer accelerator affinity / local memory as small refinements.
    if (f.direct_pci_neighbor) c -= 5;
    (void)na; (void)nb;
    return c;
  }

  static LocalityRelationship classify(const TopologyNode* na, const TopologyNode* nb,
                                       const PciAddress& a, const PciAddress& b,
                                       const LocalityFactors& f) {
    if (a == b) return LocalityRelationship::kSameDevice;
    if (f.pcie_switch_same) return LocalityRelationship::kSamePcieSwitch;
    if (f.root_complex_same) return LocalityRelationship::kSameRootComplex;
    if (f.numa_same) return LocalityRelationship::kSameNumaNode;
    if ((na && nb) && na->numa_node().valid() && nb->numa_node().valid() &&
        na->numa_node() != nb->numa_node()) {
      return LocalityRelationship::kRemoteNuma;
    }
    return LocalityRelationship::kUnknown;
  }

  friend class TopologyBuilder;

  TopologyGeneration generation_;
  std::map<PciNodeId, TopologyNode> nodes_;
  std::set<std::pair<PciNodeId, PciNodeId>> edges_;
  std::map<PciNodeId, PciNodeId> root_complex_;
  std::map<PciNodeId, PciNodeId> switch_;
};

// ---------------------------------------------------------------------------
// TopologyBuilder: validates and stages a topology before commit.
// ---------------------------------------------------------------------------
class TopologyBuilder {
 public:
  static constexpr std::size_t kMaxNodes = 4096;
  static constexpr std::size_t kMaxEdges = 16384;

  std::size_t node_count() const noexcept { return nodes_.size(); }
  std::size_t edge_count() const noexcept { return edges_.size(); }

  TopologyBuilder& add_node(TopologyNode node) {
    if (nodes_.size() >= kMaxNodes) {
      throw ResidencyError(ErrorCode::kResourceLimit, "topology node limit exceeded");
    }
    if (nodes_.count(node.id()) != 0) {
      throw ResidencyError(ErrorCode::kInvalidTopology, "duplicate topology node",
                           node.id().str());
    }
    nodes_[node.id()] = std::move(node);
    return *this;
  }

  TopologyBuilder& add_edge(PciNodeId from, PciNodeId to) {
    if (edges_.size() >= kMaxEdges) {
      throw ResidencyError(ErrorCode::kResourceLimit, "topology edge limit exceeded");
    }
    if (nodes_.count(from) == 0 || nodes_.count(to) == 0) {
      throw ResidencyError(ErrorCode::kInvalidTopology, "topology edge references missing node");
    }
    edges_.insert(std::make_pair(from, to));
    return *this;
  }

  TopologySnapshot build(TopologyGeneration generation) const {
    TopologySnapshot snap(generation);
    snap.nodes_ = nodes_;
    snap.edges_ = edges_;
    snap.finalize();
    return snap;
  }

 private:
  std::map<PciNodeId, TopologyNode> nodes_;
  std::set<std::pair<PciNodeId, PciNodeId>> edges_;

  friend class TopologySnapshot;
};

}  // namespace nicresidency
