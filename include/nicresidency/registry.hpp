#pragma once
// NicRegistry: the mutable, authority-aware store of current NIC residency
// state, plus RegistrySnapshot: an immutable read surface.
//
// Invariants enforced:
//   * no function references a nonexistent NIC;
//   * no queue references a nonexistent function/NIC;
//   * no port references a nonexistent NIC;
//   * no attachment references nonexistent resources;
//   * generations never decrease;
//   * stale WorkerBootId cannot mutate current state;
//   * stale CoordinatorEpoch cannot mutate current state;
//   * duplicate idempotent publication does not double-count.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nicresidency/error.hpp"
#include "nicresidency/id.hpp"
#include "nicresidency/records.hpp"
#include "nicresidency/topology.hpp"

namespace nicresidency {

class NicRegistry;  // fwd

// ---------------------------------------------------------------------------
// RegistrySnapshot - immutable read surface used by the policy, CLI, tests and
// benchmarks.  Never mutated after construction.
// ---------------------------------------------------------------------------
class RegistrySnapshot {
 public:
  RegistrySnapshot() = default;

  CoordinatorEpoch epoch() const noexcept { return epoch_; }

  std::size_t nic_count() const noexcept { return nics_.size(); }
  std::size_t gpu_count() const noexcept { return gpus_.size(); }
  std::size_t function_count() const noexcept { return functions_.size(); }
  std::size_t port_count() const noexcept { return ports_.size(); }
  std::size_t queue_count() const noexcept { return queues_.size(); }
  std::size_t attachment_count() const noexcept { return attachments_.size(); }

  const NicRecord* nic(NicId id) const {
    auto it = nics_.find(id);
    return it == nics_.end() ? nullptr : &it->second;
  }
  const FunctionRecord* function(FunctionId id) const {
    auto it = functions_.find(id);
    return it == functions_.end() ? nullptr : &it->second;
  }
  const PortRecord* port(PortId id) const {
    auto it = ports_.find(id);
    return it == ports_.end() ? nullptr : &it->second;
  }
  const QueueRecord* queue(QueueId id) const {
    auto it = queues_.find(id);
    return it == queues_.end() ? nullptr : &it->second;
  }
  const Attachment* attachment(AttachmentId id) const {
    auto it = attachments_.find(id);
    return it == attachments_.end() ? nullptr : &it->second;
  }
  const TopologySnapshot* topology() const noexcept {
    return topology_ ? &(*topology_) : nullptr;
  }

  const std::map<NicId, NicRecord>& nics() const noexcept { return nics_; }
  const std::map<FunctionId, FunctionRecord>& functions() const noexcept { return functions_; }
  const std::map<PortId, PortRecord>& ports() const noexcept { return ports_; }
  const std::map<QueueId, QueueRecord>& queues() const noexcept { return queues_; }
  const std::map<AttachmentId, Attachment>& attachments() const noexcept {
    return attachments_;
  }

  const std::map<NicId, NicRecord>& unordered_nics() const noexcept { return nics_; }

  const GpuRecord* gpu(GpuId id) const {
    auto it = gpus_.find(id);
    return it == gpus_.end() ? nullptr : &it->second;
  }
  const std::map<GpuId, GpuRecord>& gpus() const noexcept { return gpus_; }

  std::string summary() const;

 private:
  CoordinatorEpoch epoch_;
  std::map<NicId, NicRecord> nics_;
  std::map<FunctionId, FunctionRecord> functions_;
  std::map<PortId, PortRecord> ports_;
  std::map<QueueId, QueueRecord> queues_;
  std::map<AttachmentId, Attachment> attachments_;
  std::map<GpuId, GpuRecord> gpus_;
  std::vector<ResidencyRecord> residencies_;
  std::unique_ptr<TopologySnapshot> topology_;

  friend class NicRegistry;
};

// ---------------------------------------------------------------------------
// NicRegistry: mutable, authority-aware store.  All mutations are validated
// (references, generations, authority) and applied under one lock so a failure
// before commit leaves the prior canonical state intact.
// ---------------------------------------------------------------------------
class NicRegistry {
 public:
  struct WorkerHandle {
    WorkerId worker;
    WorkerBootId boot;
  };

  explicit NicRegistry(CoordinatorEpoch epoch) : epoch_(epoch) {
    EnsureNextEpoch(epoch_);
    next_residency_ = ResidencyId(1);
  }

  NicRegistry(const NicRegistry&) = delete;
  NicRegistry& operator=(const NicRegistry&) = delete;

  // --- authority -----------------------------------------------------------

  CoordinatorEpoch epoch() const noexcept { return epoch_; }

  void register_worker(WorkerId worker, WorkerBootId boot) {
    std::lock_guard<std::mutex> lk(mu_);
    if (workers_.count(worker) != 0 && workers_[worker].boot != boot) {
      throw ResidencyError(ErrorCode::kOwnerConflict,
                           "worker already active with a different boot id",
                           "worker=" + worker.str());
    }
    workers_[worker] = WorkerHandle{worker, boot};
  }

  // Retire a worker's dynamic authority.  Its process-owned evidence is marked
  // revalidation-required, not silently dropped or resurrected.
  void worker_retired(WorkerId worker, WorkerBootId boot) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = workers_.find(worker);
    if (it != workers_.end() && it->second.boot != boot) {
      // Stale boot id cannot retire a newer incarnation.
      return;
    }
    workers_.erase(worker);
    // Mark all NIC evidence published by this worker incarnation as
    // revalidation-required: its process-owned dynamic evidence must no longer
    // remain silently authoritative.
    auto wn = worker_nics_.find(worker);
    if (wn != worker_nics_.end()) {
      for (const NicId& nd : wn->second) {
        auto nit = nics_.find(nd);
        if (nit != nics_.end()) {
          nit->second.lifecycle = LifecycleState::kRevalidationRequired;
          nit->second.freshness = 0;
        }
      }
      worker_nics_.erase(wn);
    }
    // Mark all attachments/residency owned by this worker boot as stale.
    for (auto& kv : attachments_) {
      if (kv.second.owner_worker == worker && kv.second.owner_boot == boot) {
        kv.second.state = AttachmentState::kStale;
        kv.second.owner_worker = WorkerId();
        kv.second.owner_boot = WorkerBootId();
      }
    }
    for (auto& r : residencies_) {
      if (r.owner_worker == worker && r.owner_boot == boot) {
        r.state = ResidencyState::kRevalidationRequired;
        r.lifecycle = LifecycleState::kRevalidationRequired;
        r.owner_worker = WorkerId();
        r.owner_boot = WorkerBootId();
      }
    }
  }

  // Advance to a new coordinator epoch: clear live worker authority and mark
  // dynamic evidence revalidation-required.  Durable identities survive but
  // dynamic locality/readiness/attachment evidence does not.
  void coordinator_restart(CoordinatorEpoch new_epoch) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!(new_epoch.value() > epoch_.value())) {
      throw ResidencyError(ErrorCode::kStaleEpoch,
                           "coordinator epoch must advance monotonically",
                           new_epoch.str());
    }
    epoch_ = new_epoch;
    workers_.clear();
    worker_nics_.clear();
    for (auto& kv : functions_) {
      kv.second.ownership = OwnershipState::kUnassigned;
      kv.second.owner_worker = WorkerId();
      kv.second.owner_boot = WorkerBootId();
      kv.second.epoch = epoch_;
    }
    for (auto& kv : attachments_) {
      kv.second.state = AttachmentState::kStale;
      kv.second.owner_worker = WorkerId();
      kv.second.owner_boot = WorkerBootId();
      kv.second.epoch = epoch_;
    }
    for (auto& r : residencies_) {
      r.state = ResidencyState::kRevalidationRequired;
      r.lifecycle = LifecycleState::kRevalidationRequired;
      r.owner_worker = WorkerId();
      r.owner_boot = WorkerBootId();
      r.epoch = epoch_;
    }
  }

  // --- NICs ----------------------------------------------------------------

  NicGeneration register_nic(NicRecord rec, CoordinatorEpoch epoch,
                             WorkerId worker, WorkerBootId boot);
  void advance_nic_generation(NicId id, CoordinatorEpoch epoch, WorkerId worker,
                              WorkerBootId boot);

  // --- functions -----------------------------------------------------------

  FunctionGeneration register_function(FunctionRecord rec, CoordinatorEpoch epoch,
                                       WorkerId worker, WorkerBootId boot);
  void advance_function_generation(FunctionId id, CoordinatorEpoch epoch,
                                   WorkerId worker, WorkerBootId boot);

  // --- ports / queues ------------------------------------------------------

  PortGeneration register_port(PortRecord rec, CoordinatorEpoch epoch,
                               WorkerId worker, WorkerBootId boot);
  QueueGeneration register_queue(QueueRecord rec, CoordinatorEpoch epoch,
                                 WorkerId worker, WorkerBootId boot);
  void advance_queue_generation(QueueId id, CoordinatorEpoch epoch,
                                WorkerId worker, WorkerBootId boot);

  // --- topology ------------------------------------------------------------

  void set_topology(TopologySnapshot top, CoordinatorEpoch epoch,
                    WorkerId worker, WorkerBootId boot);

  // --- attachments ---------------------------------------------------------

  AttachmentGeneration register_attachment(Attachment att, CoordinatorEpoch epoch,
                                           WorkerId worker, WorkerBootId boot);
  void retire_attachment(AttachmentId id, CoordinatorEpoch epoch,
                         WorkerId worker, WorkerBootId boot);

  // --- accelerators -------------------------------------------------------

  void register_gpu(GpuRecord rec, CoordinatorEpoch epoch, WorkerId worker,
                    WorkerBootId boot);

  // --- residency -----------------------------------------------------------

  ResidencyRecord publish_residency(ResidencyRecord rec, CoordinatorEpoch epoch,
                                    WorkerId worker, WorkerBootId boot);
  void retire_residency(ResidencyId id, CoordinatorEpoch epoch, WorkerId worker,
                        WorkerBootId boot);

  // --- queries / snapshots -------------------------------------------------

  RegistrySnapshot snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    RegistrySnapshot s;
    s.epoch_ = epoch_;
    s.nics_ = nics_;
    s.functions_ = functions_;
    s.ports_ = ports_;
    s.queues_ = queues_;
    s.attachments_ = attachments_;
    s.gpus_ = gpus_;
    s.residencies_ = residencies_;
    if (topology_) s.topology_ = std::make_unique<TopologySnapshot>(*topology_);
    return s;
  }

  const std::vector<ResidencyRecord>& residencies() const {
    std::lock_guard<std::mutex> lk(mu_);
    return residencies_;
  }

  static constexpr std::size_t kMaxNics = 1024;
  static constexpr std::size_t kMaxGpus = 512;
  static constexpr std::size_t kMaxFunctions = 8192;
  static constexpr std::size_t kMaxPorts = 8192;
  static constexpr std::size_t kMaxQueues = 16384;
  static constexpr std::size_t kMaxAttachments = 16384;
  static constexpr std::size_t kMaxResidencies = 16384;

 private:
  void EnsureAuthority(CoordinatorEpoch epoch, WorkerId worker, WorkerBootId boot) const {
    if (epoch != epoch_) {
      throw ResidencyError(ErrorCode::kStaleEpoch,
                           "stale coordinator epoch rejected", epoch.str());
    }
    auto it = workers_.find(worker);
    if (it == workers_.end()) {
      throw ResidencyError(ErrorCode::kNotReady,
                           "worker is not a registered authority",
                           "worker=" + worker.str());
    }
    if (it->second.boot != boot) {
      throw ResidencyError(ErrorCode::kStaleBoot,
                           "stale worker boot identity rejected", boot.str());
    }
  }

  static void EnsureNextEpoch(CoordinatorEpoch e) {
    // Epoch must be valid (non-zero).
    if (!e.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "epoch must be non-zero");
    }
  }

  mutable std::mutex mu_;
  CoordinatorEpoch epoch_;
  std::map<WorkerId, WorkerHandle> workers_;
  std::map<NicId, NicRecord> nics_;
  std::map<FunctionId, FunctionRecord> functions_;
  std::map<PortId, PortRecord> ports_;
  std::map<QueueId, QueueRecord> queues_;
  std::map<AttachmentId, Attachment> attachments_;
  std::map<GpuId, GpuRecord> gpus_;
  std::vector<ResidencyRecord> residencies_;
  std::unique_ptr<TopologySnapshot> topology_;
  ResidencyId next_residency_;
  // Which NIC ids each worker has published, so their process death can mark
  // that dynamic evidence REVALIDATION_REQUIRED.
  std::map<WorkerId, std::set<NicId>> worker_nics_;
};

}  // namespace nicresidency
