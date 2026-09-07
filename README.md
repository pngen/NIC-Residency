# NIC Residency

**Version 1.0.1** &nbsp;|&nbsp; Copyright 2026 Summon Software Labs. &nbsp;|&nbsp; Apache License 2.0

NIC Residency is a vendor-neutral C++20 runtime for representing, governing, explaining, and fencing the
**residency and locality** of NIC / SmartNIC / DPU resources in accelerator infrastructure. It answers a
single, sharp systems question:

> Which NIC-side resources are resident where, relative to which compute / memory / accelerator domains,
> and under which current evidence -- and which workloads or data paths may rely on that residency without
> using stale hardware or process evidence?

The core thesis is deliberately narrow and testable: **a NIC being present does not mean it is currently the
right NIC for a workload.** Useful residency depends on where the NIC lives relative to accelerators, CPUs,
NUMA nodes, memory, queues, ports, functions, and the processes that currently own or observe those resources.
A locality decision is valid only while the hardware identity, attachment, topology, generation, ownership, and
dynamic evidence supporting it remain current.

---

## Exact systems boundary

### NIC Residency owns

- stable NIC / SmartNIC / DPU-facing **logical identity** and NIC generation;
- port and PCI / SR-IOV **function identity** (where observable), including PF / VF / subfunction / representor classes;
- PCI identity, **NUMA locality**, root-complex / PCIe-switch locality, and **accelerator affinity**;
- host-memory and NIC / DPU-local memory locality (where observable);
- **queue / resource residency** metadata;
- attachment relationships and their **authority**;
- current **resident / non-resident** state, **residency generations**, and **lifecycle**;
- topology, capability, and readiness evidence required to make residency decisions;
- deterministic **residency selection**, **locality scoring**, and **explanation**;
- **stale rejection** (stale topology, device, function, queue, attachment, residency, worker boot, coordinator epoch);
- conservative **persistence** and **recovery**;
- **REAL / SYNTHETIC / UNSUPPORTED** labeling end-to-end;
- vendor-neutral backend interfaces and optional vendor-specific discovery backends.

### NIC Residency does not own

- GPUDirect RDMA transfer execution; GPUDirect Storage; RDMA registration as a general service;
- queue-pair transport semantics; TCP/IP; Ethernet or InfiniBand routing;
- NCCL, MPI, collectives, collective scheduling, generic packet scheduling;
- generic DPU / SmartNIC dataplane programming, offload scheduling;
- GPU placement, global workload scheduling, bandwidth governance, congestion control;
- NVLink / NVSwitch topology, the PCIe Fabric, NUMA Fabric, DPU Fabric, or GPU Direct Fabric;
- the Transport Offload Scheduler, Fabric Scheduler, Communication Planner, or Resource Broker.

NIC Residency **does not implement a networking stack**. It does **not execute GPUDirect RDMA**. It does not
replace RDMA Buffer. It does not program SmartNIC / DPU dataplanes. It does not own general PCIe or NUMA
governance. It is a knowledge and authority layer: it says which NIC is resident where and under which evidence,
and it fences stale evidence. Downstream users integrate this runtime boundary into their own scheduling and
networking stack.

### Distinction from neighbouring runtimes

- **GPU Direct Fabric:** determines whether an actual direct fabric path between the GPU and the NIC is valid.
  NIC Residency may expose that this NIC is closest or currently preferred for GPU X, but it does not decide or
  execute the path; the distinction is explicit in both code and documentation.
- **DPU Fabric:** owns DPU dataplane and fabric programming. NIC Residency only models DPU locality and residency.
- **PCIe Fabric and NUMA Fabric:** own the fabric as a whole. NIC Residency reads fabric locality evidence only.
- **Resource Broker:** owns resource brokering. NIC Residency only answers residency and authority questions.

---

## Domain model

Every identity is a distinct, strongly typed C++ type (`nicresidency::Id<Tag>`) and every generation a distinct
`nicresidency::Generation<Tag>`. Raw integers or strings are never interchanged between identity categories. The
set includes: `NicId`, `NicGeneration`, `PortId`, `PortGeneration`, `FunctionId`, `FunctionGeneration`, `QueueId`,
`QueueGeneration`, `ResidencyId`, `ResidencyGeneration`, `GpuId`, `GpuGeneration`, `NumaNodeId`, `PciNodeId`,
`EndpointId`, `EndpointGeneration`, `WorkerId`, `WorkerBootId`, `CoordinatorEpoch`, `TopologyGeneration`,
`CapabilityGeneration`, `PolicyId`, `DecisionId`, `AttachmentId`, `AttachmentGeneration`.

### NIC / function / port / queue model

- `NicRecord` -- stable logical identity, OS/backend identity, PCI identity, vendor/device, function class,
  capability set, NUMA node, link state, health, lifecycle, provenance, freshness. No process-local handle is ever
  persisted as identity.
- `FunctionRecord` -- PCI / SR-IOV function identity (PF / VF / subfunction / representor / unknown), parent NIC,
  port, NUMA, capability, assigned owner, lifecycle, provenance, freshness.
- `PortRecord` -- observable NIC port with link state/speed and parent NIC / function.
- `QueueRecord` -- observable queue / resource residency with a stable backend identity and kind
  (RX / TX / COMPLETION / QUEUE_GROUP / RDMA). Queue residency is generation-bound.

### Residency model

A resource may be `ABSENT / DISCOVERED / AVAILABLE / RESIDENT / ATTACHED / DETACHED / DRAINING /
REVALIDATION_REQUIRED / STALE / FAILED / UNSUPPORTED / RETIRED`. NIC Residency never conflates present with
resident, resident with eligible, attached with healthy, or local with authoritative. A `ResidencyRecord` binds
NIC / function / port / queue identity to a target GPU / NUMA / endpoint alongside the residency generation,
attachment and topology generations, ownership, policy, worker incarnation, coordinator epoch, provenance,
freshness, and a structured explanation.

### Locality model

Locality is explicit and explainable: `SAME_DEVICE / SAME_PCIE_SWITCH / SAME_ROOT_COMPLEX / SAME_NUMA_NODE /
REMOTE_NUMA / HOST_LOCAL / ACCELERATOR_LOCAL / UNKNOWN / UNSUPPORTED`. A `LocalityFactors` record preserves all
named contributing factors (root-complex, PCIe-switch, NUMA, accelerator-affinity, CPU-local, local-memory,
host/accelerator-local, direct PCI neighbour) plus a stable numeric cost. The cost is a named aggregate, never the
whole model. Each decision labels which locality facts are REAL / DERIVED / UNKNOWN.

### Accelerator affinity

A NIC may have direct physical locality to a GPU, a shared root complex, a shared PCIe switch, remote-NUMA, or an
unknown/unsupported relationship. NIC Residency may expose that this NIC is closest or currently preferred for GPU
X; it does **not** claim GPUDirect merely because accelerator affinity is favourable. GPU Direct Fabric decides the
actual direct path.

---

## Generations, authority, and lifecycle

- NIC, function, queue, topology, capability, attachment, and residency generations are **monotonic** and
  agent-bound: a NIC generation advance invalidates dependent functions / queues / attachments / residencies; a
  function generation advance invalidates dependent queues and residencies; a topology change invalidates dependent
  attachments; a queue generation advance invalidates dependent residency.
- **Worker authority** is explicit: each worker carries a `WorkerId` and a `WorkerBootId` (a per-incarnation token).
  Worker process-owned dynamic evidence is only authoritative while that worker is alive; on real process death its
  NIC / function / attachment / residency evidence becomes `REVALIDATION_REQUIRED`. Stale `WorkerBootId` traffic is
  rejected.
- **Coordinator epoch** fences process restart: `CoordinatorEpoch` advances monotonically; prior-epoch traffic is
  rejected; live worker authority is cleared; dynamic evidence is marked revalidation-required.
- **Lifecycle** is guarded and testable: `DISCOVERED -> AVAILABLE -> RESERVED_FOR_ATTACHMENT -> ATTACHED -> RESIDENT ->
  DRAINING -> DETACHED`, plus `REVALIDATION_REQUIRED / FAILED / RETIRED`. Invalid transitions are rejected.

---

## Deterministic residency policy

Hard constraints are applied first and **fail closed**: the NIC / function / topology / port / queue / capability
must be current; required NUMA / root-complex / switch locality, ownership, readiness, and freshness must hold;
policy exclusions reject. Then eligible candidates are ranked by named factors (same PCIe switch, same root complex,
same NUMA node, accelerator affinity, CPU locality, local memory, capability completeness, readiness, freshness) with
stable tie-breaking. The final ordering is a strict total order on `(rank_score, NIC id)`, so **insertion order never
changes the result** -- proven by `example_deterministic_tiebreak` and the randomized property suite.

`UNKNOWN` never silently becomes `RESIDENCY_ALLOWED`. `SYNTHETIC` evidence is inadmissible as real residency unless
the policy explicitly allows synthetic (fail closed by default).

---

## Backend architecture

The core is vendor-neutral. `Backend::discover(const DiscoveryContext&)` returns a `DiscoveryResult` of records
already labeled with provenance. Implemented backends:

- **`SystemBackend` (Windows)** -- REAL host discovery via SetupAPI (network class, PCI identity from the device
  location string, vendor/device from the hardware ID) and the IP Helper API (link state).
- **`NvidiaAffinityBackend`** -- REAL accelerator identity / PCI / NUMA via **NVML loaded dynamically** (no link-time
  NVIDIA SDK dependency). Reports `UNSUPPORTED` cleanly when NVML is absent.
- **`SyntheticBackend`** -- a rigorous, deterministic fixture backend for ten scenarios that cannot be exercised on
  current hardware (local-to-GPU, two-NUMA, root-complex / switch preference, remote-NUMA fallback, SmartNIC local
  memory, DPU local memory, PF with multiple VFs, dense candidate set, unsupported capability). Every record is
  labeled `SYNTHETIC[SYNTHETIC_FIXTURE]` and never becomes REAL after reload.
- **`UnsupportedBackend`** -- an explicit no-op that claims nothing.

Optional vendor SDKs are not required for the core package, which builds and runs without CUDA / NVML / RDMA
SDKs. SmartNIC / DPU / RDMA capabilities are reported `UNSUPPORTED` when the platform genuinely does not expose
them -- never inferred from a product name or link speed.

---

## Persistence

`PersistenceStore` persists only architecturally durable state (stable logical NIC identity, PCI identity, static
capability knowledge, durable device / GPU identity, policy, and an inspectable residency history). It uses an
explicit format version, bounded fields, checked arithmetic, an FNV-1a integrity checksum, and atomic replacement
(via `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` on Windows). It rejects corruption,
truncation, trailing garbage, invalid enums, duplicate identities, dangling references, invalid generations, absurd
counts, and unsupported mandatory future versions. **Persisted dynamic evidence never recovers as fresh** -- on
recovery, NIC lifecycle is `REVALIDATION_REQUIRED` and freshness is cleared.

---

## Concurrency

`NicRegistry` owns a single mutation lock; all mutations are validated (references, generations, authority) before
commit, so a failure leaves prior canonical state intact. Read paths return an immutable `RegistrySnapshot` copy, so
queries never observe partially-published state. A manual deadlock / lock-reentrancy audit was performed against the
listed hazard classes (read-then-write on one lock, callbacks under locks, worker death vs. query, removal racing
lookup, persistence racing mutation, join under lock, etc.); the design avoids them by not invoking callbacks under
the lock and by snapshotting for reads. A deterministic multi-threaded race test exercises concurrent mutation and
query against the invariant suite.

---

## REAL / SYNTHETIC / UNSUPPORTED

Every hardware-facing record carries one of `REAL / SYNTHETIC / UNSUPPORTED` plus provenance source tags
(`OPERATING_SYSTEM`, `PCI`, `NUMA`, `CUDA`, `NVML`, `RDMA_PROVIDER`, `VERBS`, `NET_ADAPTER_API`, `VENDOR_SDK`,
`DPU_RUNTIME`, `PERSISTED`, `DERIVED`, `SYNTHETIC_FIXTURE`). The kind is preserved end-to-end and never silently
changes across a reload or a derivation. `SYNTHETIC` never becomes `REAL`; `UNSUPPORTED` never becomes supported
without new evidence.

---

## Build

Requires a C++20 compiler, CMake 3.21 or later, and (on Windows) MSVC plus the Windows SDK. Example:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Options: `NICRESIDENCY_BUILD_TESTS`, `NICRESIDENCY_BUILD_CLI`, `NICRESIDENCY_BUILD_EXAMPLES`,
`NICRESIDENCY_BUILD_BENCHMARKS`, `NICRESIDENCY_BUILD_SHARED`.

---

## Inspection CLI

`nic_residency_inspect` lists NICs / functions / ports / queues, shows PCI / NUMA / accelerator affinity / topology
generation, inspects attachments and residency, resolves the best resident NIC for a workload context, explains
rejected candidates, and shows REAL / SYNTHETIC / UNSUPPORTED provenance. `--system` runs real host discovery,
`--synthetic <scenario>` runs a fixture scenario, `--json` emits deterministic JSON, and `--version` / `--help` work
as expected.

---

## Tests

- `nicresidency_tests` -- core semantics (synthetic selection, stale-epoch rejection, persistence round-trip and
  revalidation, corruption / truncation / trailing-garbage rejection).
- `nicresidency_authority_tests` -- a **real OS subprocess** worker is spawned, publishes a framed evidence frame, is
  `TerminateProcess`-killed, its process-owned evidence becomes `REVALIDATION_REQUIRED`, stale `WorkerBootId` traffic
  is rejected, and a fresh worker incarnation restores authority; plus a coordinator-restart epoch-advance proof and a
  **real OS-process coordinator restart proof**: the `nic_residency_coordinator` is launched as its own process,
  established under Worker A and an authoritative residency decision, persisted and then `TerminateProcess`-killed,
  relaunched fresh, `CoordinatorEpoch` advances, durable identity/capability state is recovered
  `REVALIDATION_REQUIRED`, live worker/process authority is not silently recovered, stale prior-epoch traffic is
  rejected, and residency authority returns only after Worker A' re-establishes under fresh evidence.
- `nicresidency_invariant_tests` -- reference / invariant checks, monotonic generations, SYNTHETIC / UNKNOWN
  fail-closed behaviour, a **seeded randomized property suite** (800 ops, invariants checked after every op), a
  **deterministic multi-threaded race test**, and adversarial duplicate / dangling-reference cases.

No test uses any timeout, watchdog, or forced-termination-as-pass logic. Tests run plainly and naturally.

---

## Examples

Ten examples in `examples/` compile against the installed public headers and demonstrate basic discovery, NUMA-aware
and accelerator-affinity selection, deterministic tie-breaking, stale-generation rejection, attachment-change
invalidation, worker incarnation fencing, synthetic SmartNIC / DPU scenarios, real system discovery, and clean
UNSUPPORTED SmartNIC / DPU behaviour.

---

## Benchmarks

`benchmarks/` measure completed work (NIC registration, topology build plus locality query, candidate selection,
queue lookup, persistence save and load) and report the workload context (NIC / function / queue / attachment /
candidate counts and topology size). Synthetic locality benchmarks are clearly labeled synthetic and are never
presented as hardware performance.

---

## Install and downstream integration

The package installs the library, public headers, and a CMake package config. A downstream project may use:

```cmake
find_package(NICResidency REQUIRED)
target_link_libraries(myapp PRIVATE NICResidency::nicresidency)
```

An independent consumer outside the repository tree is built against the installed prefix to prove this. The
vendor-neutral core does not force consumers to install CUDA / NVML / RDMA / vendor SDKs.

---

## Real hardware validation (this machine)

On the validating Windows host, `nic_residency_inspect --system` reported:

- **Real system NIC discovery:** two real PCI NICs -- a Qualcomm FastConnect 7800 Wi-Fi adapter (PCI `0000:0d:00.0`)
  and a Realtek PCIe 5GbE Family Controller (PCI `0000:0e:00.0`) -- with `REAL[OPERATING_SYSTEM,NET_ADAPTER_API]`
  provenance and real PCI identity.
- **Real accelerator discovery and GPU PCI identity:** an NVIDIA GeForce RTX 5090 at PCI `0000:01:00.0`, `REAL[NVML]`.
- **Real NIC / GPU locality derivation:** derived from real PCI BDFs (same PCI segment, different buses, hence
  `SAME_ROOT_COMPLEX`, labeled DERIVED) -- never treated as GPUDirect. Both devices share NUMA node 0 on this host.
- **RDMA-capable hardware:** not present, reported UNSUPPORTED.
- **SmartNIC / DPU hardware:** not present, reported UNSUPPORTED; no SmartNIC / DPU / local-memory claim.
- **NIC / DPU-local memory:** not observable on the real surface, reported UNSUPPORTED.

The per-device NUMA node is not exposed by a documented `DEVPROPKEY` in the shipped Windows SDK; NIC NUMA is
reported `UNKNOWN` and locality is derived from the real PCI hierarchy and records. GPU NUMA is obtained via NVML
when the driver exposes it.

### Genuine limitations

- Only the Windows system backend is implemented to production quality; there is no Linux sysfs / PCI / NUMA backend.
- SR-IOV PF / VF topology, RDMA capability, SmartNIC / DPU detection, offload capability, and NIC / DPU-local memory
  are not offered by this host or backend and are honestly reported `UNSUPPORTED`.
- NIC-level queue enumeration is not exposed by the Windows backend; queue residency is modeled for backends that
  expose it and exercised via the synthetic backend.
- Coordinator restart is proven both at the registry / state level and with a real OS-process coordinator: the
  coordinator is launched as an independent process, established under a live worker, persisted and terminated for real,
  then relaunched fresh with a strictly advanced `CoordinatorEpoch`, durable identity/capability state recovered
  `REVALIDATION_REQUIRED`, stale prior-epoch traffic rejected, and residency authority gated behind Worker A/A'
  re-establishment under fresh evidence; worker process death is proven with a real OS process kill.
- `std::random_device` provides boot-id entropy; boot ids are per-incarnation and are never persisted as authority.

---

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
