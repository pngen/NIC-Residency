// nic_authority_worker: a real worker process that publishes a NIC evidence
// frame under its own WorkerBootId and then stays alive until killed, so a
// test can prove real process death invalidates its dynamic authority.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif

#include "nicresidency/authority.hpp"
#include "nicresidency/engine.hpp"
#include "nicresidency/records.hpp"

using namespace nicresidency;

int main(int argc, char** argv) {
  std::string framePath = (argc > 1) ? argv[1] : "worker_frame.bin";
  WorkerId myId = (argc > 2) ? WorkerId(std::strtoull(argv[2], nullptr, 0)) : WorkerId(1);
  CoordinatorEpoch epoch = (argc > 4) ? CoordinatorEpoch(std::strtoull(argv[4], nullptr, 0))
                                      : CoordinatorEpoch(1);
  WorkerBootId boot = fresh_boot_id();

  std::uint64_t gen = (argc > 3) ? std::strtoull(argv[3], nullptr, 0) : 1;
  NicRecord nic;
  nic.id = NicId(0x1000);
  nic.generation = NicGeneration(gen);
  nic.os_name = "worker-published-nic";
  nic.os_description = "evidence published by a real worker process";
  nic.pci = PciAddress(0, 0x01, 0, 0);
  nic.numa_node = NumaNodeId(1);
  nic.capability.set_rdma_capable(true);
  nic.capability.set_provenance(Provenance(EvidenceKind::kReal, {Source::kRdmaProvider}));
  nic.provenance = Provenance(EvidenceKind::kReal, {Source::kRdmaProvider});
  nic.lifecycle = LifecycleState::kAvailable;
  nic.freshness = 1;

  std::string payload = FrameCodec::payload_publish_nic(myId, boot, epoch, nic);
  std::string frame = FrameCodec::encode(ProtocolMessageType::kPublishNic, payload);
  { std::ofstream f(framePath, std::ios::binary | std::ios::trunc); f.write(frame.data(), static_cast<std::streamsize>(frame.size())); }

  // Signal readiness, then hold the process alive so the test can kill it.
  std::printf("READY boot=%s\n", boot.str().c_str());
  std::fflush(stdout);
  for (;;) {
#ifdef _WIN32
    ::Sleep(1000);
#else
    std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
  }
  return 0;
}