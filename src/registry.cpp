
// Registry implementation: transactional, authority-aware mutations.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/registry.hpp"

#include <algorithm>
#include <sstream>

namespace nicresidency {

namespace {

bool nic_equal(const NicRecord& a, const NicRecord& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.os_name == b.os_name &&
         a.os_description == b.os_description && a.pci == b.pci &&
         a.vendor_id == b.vendor_id && a.device_id == b.device_id &&
         a.subsystem_vendor_id == b.subsystem_vendor_id && a.subsystem_id == b.subsystem_id &&
         a.revision == b.revision && a.device_kind == b.device_kind &&
         a.function_class == b.function_class && a.numa_node == b.numa_node &&
         a.capability == b.capability && a.provenance == b.provenance;
}

bool function_equal(const FunctionRecord& a, const FunctionRecord& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.parent_nic == b.parent_nic &&
         a.function_class == b.function_class && a.port == b.port &&
         a.numa_node == b.numa_node && a.capability == b.capability &&
         a.provenance == b.provenance;
}

bool port_equal(const PortRecord& a, const PortRecord& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.parent_nic == b.parent_nic &&
         a.parent_function == b.parent_function && a.os_name == b.os_name &&
         a.numa_node == b.numa_node && a.provenance == b.provenance;
}

bool queue_equal(const QueueRecord& a, const QueueRecord& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.parent_function == b.parent_function &&
         a.parent_nic == b.parent_nic && a.kind == b.kind && a.provenance == b.provenance;
}

bool attachment_equal(const Attachment& a, const Attachment& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.nic == b.nic &&
         a.function == b.function && a.port == b.port && a.queue == b.queue && a.gpu == b.gpu &&
         a.numa == b.numa && a.endpoint == b.endpoint &&
         a.topology_generation == b.topology_generation && a.state == b.state &&
         a.owner_worker == b.owner_worker && a.owner_boot == b.owner_boot && a.epoch == b.epoch &&
         a.provenance == b.provenance;
}

bool residency_equal(const ResidencyRecord& a, const ResidencyRecord& b) noexcept {
  return a.id == b.id && a.generation == b.generation && a.nic == b.nic &&
         a.function == b.function && a.port == b.port && a.queue == b.queue &&
         a.target_gpu == b.target_gpu && a.target_numa == b.target_numa &&
         a.target_endpoint == b.target_endpoint && a.state == b.state &&
         a.attachment_generation == b.attachment_generation &&
         a.topology_generation == b.topology_generation &&
         a.capability_generation == b.capability_generation && a.ownership == b.ownership &&
         a.owner_worker == b.owner_worker && a.owner_boot == b.owner_boot && a.epoch == b.epoch &&
         a.policy == b.policy && a.provenance == b.provenance && a.lifecycle == b.lifecycle;
}

}  // namespace

void NicRegistry::invalidate_nic_dependents_locked(NicId id) {
  for (auto& kv : functions_) {
    if (kv.second.parent_nic == id) {
      kv.second.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
  for (auto& kv : queues_) {
    if (kv.second.parent_nic == id) {
      kv.second.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
  for (auto& kv : attachments_) {
    if (kv.second.nic == id) {
      kv.second.state = AttachmentState::kStale;
    }
  }
  for (auto& r : residencies_) {
    if (r.nic == id) {
      r.state = ResidencyState::kRevalidationRequired;
      r.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
}

void NicRegistry::invalidate_function_dependents_locked(FunctionId id) {
  // A queue belonging to FunctionGeneration N must not remain current after the
  // function advances; dependent attachments and residencies are invalidated.
  for (auto& kv : queues_) {
    if (kv.second.parent_function == id) {
      kv.second.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
  for (auto& kv : attachments_) {
    if (kv.second.function == id) kv.second.state = AttachmentState::kStale;
  }
  for (auto& r : residencies_) {
    if (r.function == id) {
      r.state = ResidencyState::kRevalidationRequired;
      r.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
}

void NicRegistry::invalidate_queue_dependents_locked(QueueId id) {
  for (auto& r : residencies_) {
    if (r.queue == id) {
      r.state = ResidencyState::kRevalidationRequired;
      r.lifecycle = LifecycleState::kRevalidationRequired;
    }
  }
}

std::string RegistrySnapshot::summary() const {
  std::ostringstream os;
  os << "epoch=" << epoch_.str() << " nics=" << nics_.size()
     << " gpus=" << gpus_.size() << " funcs=" << functions_.size()
     << " ports=" << ports_.size() << " queues=" << queues_.size()
     << " attachments=" << attachments_.size();
  if (topology_) os << " topo_nodes=" << topology_->node_count();
  return os.str();
}

NicGeneration NicRegistry::register_nic(NicRecord rec, CoordinatorEpoch epoch,
                                        WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "NIC id must be valid");
  }
  if (nics_.size() >= kMaxNics) {
    throw ResidencyError(ErrorCode::kResourceLimit, "NIC count limit exceeded");
  }
  const NicId id = rec.id;

  auto it = nics_.find(id);
  if (it == nics_.end()) {
    if (!rec.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "new NIC generation must be non-zero");
    }
    rec.freshness = 0;  // dynamic freshness is not an authority; set on publish
    nics_[id] = std::move(rec);
    // Only an accepted publication makes this worker a publisher of the NIC.
    worker_nics_[worker].insert(id);
    return nics_[id].generation;
  }

  // Same id already present.
  NicRecord existing = it->second;
  if (rec.generation == existing.generation) {
    if (nic_equal(rec, existing)) {
      // Idempotent duplicate: no state change, no double count.
      worker_nics_[worker].insert(id);
      return existing.generation;
    }
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "conflicting duplicate NIC publication at same generation",
                         id.str());
  }
  if (rec.generation < existing.generation) {
    throw ResidencyError(ErrorCode::kStaleNic, "stale NIC generation rejected", id.str());
  }
  // Advance: replace content, enforce monotonic generation, and invalidate the
  // dependents exactly as advance_nic_generation does.
  rec.freshness = 0;
  it->second = std::move(rec);
  worker_nics_[worker].insert(id);
  invalidate_nic_dependents_locked(id);
  return it->second.generation;
}

void NicRegistry::advance_nic_generation(NicId id, CoordinatorEpoch epoch, WorkerId worker,
                                         WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  auto it = nics_.find(id);
  if (it == nics_.end()) {
    throw ResidencyError(ErrorCode::kNotFound, "NIC not found", id.str());
  }
  it->second.generation = it->second.generation.next();
  // Invalidate dependent functions, queues, attachments, residencies.
  invalidate_nic_dependents_locked(id);
}

FunctionGeneration NicRegistry::register_function(FunctionRecord rec, CoordinatorEpoch epoch,
                                                  WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid() || !rec.parent_nic.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "function id and parent NIC id required");
  }
  if (nics_.count(rec.parent_nic) == 0) {
    throw ResidencyError(ErrorCode::kInvalidTopology,
                         "function references nonexistent NIC", rec.parent_nic.str());
  }
  if (functions_.size() >= kMaxFunctions) {
    throw ResidencyError(ErrorCode::kResourceLimit, "function count limit exceeded");
  }
  auto it = functions_.find(rec.id);
  if (it == functions_.end()) {
    if (!rec.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "new function generation must be non-zero");
    }
    functions_[rec.id] = std::move(rec);
    return functions_[rec.id].generation;
  }
  FunctionRecord existing = it->second;
  if (rec.generation == existing.generation) {
    if (function_equal(rec, existing)) return existing.generation;
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "conflicting duplicate function publication at same generation",
                         rec.id.str());
  }
  if (rec.generation < existing.generation) {
    throw ResidencyError(ErrorCode::kStaleFunction, "stale function generation rejected",
                         rec.id.str());
  }
  const FunctionId id = rec.id;
  it->second = std::move(rec);
  invalidate_function_dependents_locked(id);
  return it->second.generation;
}

void NicRegistry::advance_function_generation(FunctionId id, CoordinatorEpoch epoch,
                                              WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  auto it = functions_.find(id);
  if (it == functions_.end()) {
    throw ResidencyError(ErrorCode::kNotFound, "function not found", id.str());
  }
  it->second.generation = it->second.generation.next();
  invalidate_function_dependents_locked(id);
}

PortGeneration NicRegistry::register_port(PortRecord rec, CoordinatorEpoch epoch,
                                          WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid() || !rec.parent_nic.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "port id and parent NIC id required");
  }
  if (nics_.count(rec.parent_nic) == 0) {
    throw ResidencyError(ErrorCode::kInvalidTopology,
                         "port references nonexistent NIC", rec.parent_nic.str());
  }
  if (rec.parent_function.valid() && functions_.count(rec.parent_function) == 0) {
    throw ResidencyError(ErrorCode::kInvalidTopology,
                         "port references nonexistent function", rec.parent_function.str());
  }
  if (ports_.size() >= kMaxPorts) {
    throw ResidencyError(ErrorCode::kResourceLimit, "port count limit exceeded");
  }
  auto it = ports_.find(rec.id);
  if (it == ports_.end()) {
    if (!rec.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "new port generation must be non-zero");
    }
    ports_[rec.id] = std::move(rec);
    return ports_[rec.id].generation;
  }
  PortRecord existing = it->second;
  if (rec.generation == existing.generation) {
    if (port_equal(rec, existing)) return existing.generation;
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "conflicting duplicate port publication at same generation", rec.id.str());
  }
  if (rec.generation < existing.generation) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "stale port generation rejected",
                         rec.id.str());
  }
  it->second = std::move(rec);
  return it->second.generation;
}

QueueGeneration NicRegistry::register_queue(QueueRecord rec, CoordinatorEpoch epoch,
                                            WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid() || !rec.parent_function.valid() || !rec.parent_nic.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "queue id, parent function, and parent NIC id required");
  }
  if (functions_.count(rec.parent_function) == 0) {
    throw ResidencyError(ErrorCode::kInvalidTopology,
                         "queue references nonexistent function", rec.parent_function.str());
  }
  if (nics_.count(rec.parent_nic) == 0) {
    throw ResidencyError(ErrorCode::kInvalidTopology,
                         "queue references nonexistent NIC", rec.parent_nic.str());
  }
  if (queues_.size() >= kMaxQueues) {
    throw ResidencyError(ErrorCode::kResourceLimit, "queue count limit exceeded");
  }
  auto it = queues_.find(rec.id);
  if (it == queues_.end()) {
    if (!rec.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "new queue generation must be non-zero");
    }
    queues_[rec.id] = std::move(rec);
    return queues_[rec.id].generation;
  }
  QueueRecord existing = it->second;
  if (rec.generation == existing.generation) {
    if (queue_equal(rec, existing)) return existing.generation;
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "conflicting duplicate queue publication at same generation", rec.id.str());
  }
  if (rec.generation < existing.generation) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "stale queue generation rejected",
                         rec.id.str());
  }
  const QueueId id = rec.id;
  it->second = std::move(rec);
  invalidate_queue_dependents_locked(id);
  return it->second.generation;
}

void NicRegistry::advance_queue_generation(QueueId id, CoordinatorEpoch epoch, WorkerId worker,
                                           WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  auto it = queues_.find(id);
  if (it == queues_.end()) {
    throw ResidencyError(ErrorCode::kNotFound, "queue not found", id.str());
  }
  it->second.generation = it->second.generation.next();
  invalidate_queue_dependents_locked(id);
}

void NicRegistry::set_topology(TopologySnapshot top, CoordinatorEpoch epoch, WorkerId worker,
                               WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  // Topology must not regress in generation.
  if (topology_ && top.generation() < topology_->generation()) {
    throw ResidencyError(ErrorCode::kStaleTopology, "stale topology generation rejected",
                         top.generation().str());
  }
  topology_ = std::make_unique<TopologySnapshot>(std::move(top));
  // A topology change invalidates dependent attachments/residencies.
  for (auto& kv : attachments_) {
    kv.second.state = AttachmentState::kStale;
  }
  for (auto& r : residencies_) {
    r.state = ResidencyState::kRevalidationRequired;
    r.lifecycle = LifecycleState::kRevalidationRequired;
  }
}

AttachmentGeneration NicRegistry::register_attachment(Attachment att, CoordinatorEpoch epoch,
                                                      WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!att.id.valid() || !att.nic.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "attachment id and NIC id required");
  }
  if (nics_.count(att.nic) == 0) {
    throw ResidencyError(ErrorCode::kInvalidAttachment,
                         "attachment references nonexistent NIC", att.nic.str());
  }
  if (att.function.valid() && functions_.count(att.function) == 0) {
    throw ResidencyError(ErrorCode::kInvalidAttachment,
                         "attachment references nonexistent function", att.function.str());
  }
  if (att.port.valid() && ports_.count(att.port) == 0) {
    throw ResidencyError(ErrorCode::kInvalidAttachment,
                         "attachment references nonexistent port", att.port.str());
  }
  if (att.queue.valid() && queues_.count(att.queue) == 0) {
    throw ResidencyError(ErrorCode::kInvalidAttachment,
                         "attachment references nonexistent queue", att.queue.str());
  }
  if (attachments_.size() >= kMaxAttachments) {
    throw ResidencyError(ErrorCode::kResourceLimit, "attachment count limit exceeded");
  }
  // The attachment is process-owned evidence of the publishing incarnation, so
  // that incarnation's death fences it.
  att.owner_worker = worker;
  att.owner_boot = boot;
  att.epoch = epoch_;
  auto it = attachments_.find(att.id);
  if (it == attachments_.end()) {
    if (!att.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument,
                           "new attachment generation must be non-zero");
    }
    attachments_[att.id] = std::move(att);
    return attachments_[att.id].generation;
  }
  if (att.generation < it->second.generation) {
    throw ResidencyError(ErrorCode::kInvalidAttachment, "stale attachment generation rejected",
                         att.id.str());
  }
  if (att.generation == it->second.generation) {
    // Same generation: only an identical republication is accepted, so an
    // attachment invalidated since cannot be restored without a new generation.
    if (attachment_equal(att, it->second)) return it->second.generation;
    throw ResidencyError(ErrorCode::kInvalidAttachment,
                         "conflicting duplicate attachment publication at same generation",
                         att.id.str());
  }
  it->second = std::move(att);
  return it->second.generation;
}

void NicRegistry::register_gpu(GpuRecord rec, CoordinatorEpoch epoch, WorkerId worker,
                                      WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "GPU id must be valid");
  }
  if (gpus_.size() >= kMaxGpus) {
    throw ResidencyError(ErrorCode::kResourceLimit, "GPU count limit exceeded");
  }
  auto it = gpus_.find(rec.id);
  if (it == gpus_.end()) {
    if (!rec.generation.valid()) {
      throw ResidencyError(ErrorCode::kInvalidArgument, "new GPU generation must be non-zero");
    }
    gpus_[rec.id] = std::move(rec);
    return;
  }
  if (rec.generation < it->second.generation) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "stale GPU generation rejected",
                         rec.id.str());
  }
  it->second = std::move(rec);
}

void NicRegistry::retire_attachment(AttachmentId id, CoordinatorEpoch epoch, WorkerId worker,
                                    WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  auto it = attachments_.find(id);
  if (it == attachments_.end()) {
    throw ResidencyError(ErrorCode::kNotFound, "attachment not found", id.str());
  }
  it->second.state = AttachmentState::kRevoked;
}

ResidencyRecord NicRegistry::publish_residency(ResidencyRecord rec, CoordinatorEpoch epoch,
                                               WorkerId worker, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  if (!rec.id.valid() || !rec.nic.valid()) {
    throw ResidencyError(ErrorCode::kInvalidArgument, "residency id and NIC id required");
  }
  if (nics_.count(rec.nic) == 0) {
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "residency references nonexistent NIC", rec.nic.str());
  }
  if (rec.function.valid() && functions_.count(rec.function) == 0) {
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "residency references nonexistent function", rec.function.str());
  }
  if (rec.port.valid() && ports_.count(rec.port) == 0) {
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "residency references nonexistent port", rec.port.str());
  }
  if (rec.queue.valid() && queues_.count(rec.queue) == 0) {
    throw ResidencyError(ErrorCode::kInvalidArgument,
                         "residency references nonexistent queue", rec.queue.str());
  }
  // The residency is process-owned evidence of the publishing incarnation, so
  // that incarnation's death fences it.
  rec.owner_worker = worker;
  rec.owner_boot = boot;
  rec.epoch = epoch_;
  // One current binding per residency id, with monotonic generations.
  for (auto& r : residencies_) {
    if (r.id != rec.id) continue;
    if (rec.generation < r.generation) {
      throw ResidencyError(ErrorCode::kStaleResidency, "stale residency generation rejected",
                           rec.id.str());
    }
    if (rec.generation == r.generation) {
      // Idempotent duplicate: no double count.  Anything else at the same
      // generation (e.g. restoring a binding invalidated since) is rejected.
      if (residency_equal(rec, r)) return r;
      throw ResidencyError(ErrorCode::kInvalidArgument,
                           "conflicting duplicate residency publication at same generation",
                           rec.id.str());
    }
    r = std::move(rec);  // a newer generation replaces the binding
    return r;
  }
  if (residencies_.size() >= kMaxResidencies) {
    throw ResidencyError(ErrorCode::kResourceLimit, "residency count limit exceeded");
  }
  residencies_.push_back(std::move(rec));
  return residencies_.back();
}

void NicRegistry::retire_residency(ResidencyId id, CoordinatorEpoch epoch, WorkerId worker,
                                   WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mu_);
  EnsureAuthority(epoch, worker, boot);
  for (auto& r : residencies_) {
    if (r.id == id) {
      r.state = ResidencyState::kRetired;
      r.lifecycle = LifecycleState::kRetired;
      return;
    }
  }
  throw ResidencyError(ErrorCode::kNotFound, "residency not found", id.str());
}

}  // namespace nicresidency
