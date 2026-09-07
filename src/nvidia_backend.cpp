
// NvidiaAffinityBackend: real accelerator identity / PCI / NUMA discovery via
// NVML.  NVML is loaded dynamically so the vendor-neutral core never links to
// an NVIDIA SDK; if NVML is absent the backend declares UNSUPPORTED.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/system_backend.hpp"

#include <windows.h>

#include <cstring>
#include <cstdlib>
#include <string>

namespace nicresidency {

namespace {

struct NvmlPciInfoV2 {
  char busIdLegacy[16];
  unsigned int domain;
  unsigned int bus;
  unsigned int device;
  unsigned int pciDeviceId;
  unsigned int pciSubSystemId;
  char busId[32];
};

struct NvmlApi {
  bool ok{false};
  void* handle{nullptr};
  int (*init)(void){nullptr};
  int (*shutdown)(void){nullptr};
  int (*getCount)(unsigned int*){nullptr};
  int (*getHandleByIndex)(unsigned int, void**){nullptr};
  int (*getName)(void*, char*, unsigned int){nullptr};
  int (*getPciInfo)(void*, NvmlPciInfoV2*){nullptr};
  int (*getNumaNode)(void*, unsigned int*){nullptr};

  static void* sym(void* h, const char* name) {
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(h), name));
  }
  static NvmlApi load() {
    NvmlApi a;
    HMODULE h = ::LoadLibraryW(L"nvml.dll");
    if (h == nullptr) return a;
    a.init = reinterpret_cast<int (*)(void)>(sym(h, "nvmlInit_v2"));
    a.shutdown = reinterpret_cast<int (*)(void)>(sym(h, "nvmlShutdown"));
    a.getCount = reinterpret_cast<int (*)(unsigned int*)>(sym(h, "nvmlDeviceGetCount_v2"));
    a.getHandleByIndex =
        reinterpret_cast<int (*)(unsigned int, void**)>(sym(h, "nvmlDeviceGetHandleByIndex_v2"));
    a.getName = reinterpret_cast<int (*)(void*, char*, unsigned int)>(sym(h, "nvmlDeviceGetName"));
    a.getPciInfo =
        reinterpret_cast<int (*)(void*, NvmlPciInfoV2*)>(sym(h, "nvmlDeviceGetPciInfo_v2"));
    a.getNumaNode =
        reinterpret_cast<int (*)(void*, unsigned int*)>(sym(h, "nvmlDeviceGetNumaNode"));
    if (a.init == nullptr || a.shutdown == nullptr || a.getCount == nullptr ||
        a.getHandleByIndex == nullptr || a.getPciInfo == nullptr) {
      ::FreeLibrary(h);
      return a;
    }
    a.handle = h;
    a.ok = true;
    return a;
  }
};

}  // namespace

bool NvidiaAffinityBackend::available() noexcept {
  NvmlApi a = NvmlApi::load();
  if (!a.ok) return false;
  if (a.init() != 0) {
    ::FreeLibrary(static_cast<HMODULE>(a.handle));
    return false;
  }
  a.shutdown();
  ::FreeLibrary(static_cast<HMODULE>(a.handle));
  return true;
}

DiscoveryResult NvidiaAffinityBackend::discover(const DiscoveryContext& ctx) const {
  DiscoveryResult out;
  NvmlApi a = NvmlApi::load();
  if (!a.ok) {
    throw ResidencyError(ErrorCode::kBackendUnavailable,
                         "NVML is not available; accelerator affinity UNSUPPORTED");
  }
  if (a.init() != 0) {
    ::FreeLibrary(static_cast<HMODULE>(a.handle));
    throw ResidencyError(ErrorCode::kBackendUnavailable, "NVML init failed");
  }
  unsigned int count = 0;
  if (a.getCount(&count) != 0) {
    a.shutdown();
    ::FreeLibrary(static_cast<HMODULE>(a.handle));
    throw ResidencyError(ErrorCode::kBackendError, "NVML device count failed");
  }
  out.notes.push_back("nvidia-affinity backend: NVML present, " +
                      std::to_string(count) + " device(s)");
  for (unsigned int i = 0; i < count && i < ctx.max_gpus; ++i) {
    void* dev = nullptr;
    if (a.getHandleByIndex(i, &dev) != 0) continue;
    char name[128] = {0};
    a.getName(dev, name, sizeof(name));
    NvmlPciInfoV2 pci;
    std::memset(&pci, 0, sizeof(pci));
    if (a.getPciInfo(dev, &pci) != 0) continue;

    // NVML exposes domain/bus/device and encodes function in the busId string
    // (NVML GPUs are function 0 in the legacy format).  Parse function from the
    // busId tail "domain:bus:device.function" so we keep the exact identity.
    unsigned int fn = 0;
    std::string busIdStr(pci.busId);
    std::size_t dot = busIdStr.rfind('.');
    if (dot != std::string::npos) fn = static_cast<unsigned int>(std::strtoul(busIdStr.c_str() + dot + 1, nullptr, 10));

    GpuRecord gpu;
    gpu.id = GpuId(static_cast<std::uint64_t>(pci.domain) << 16 |
                   (static_cast<std::uint64_t>(pci.bus) << 8) |
                   (static_cast<std::uint64_t>(pci.device) << 3) |
                   static_cast<std::uint64_t>(fn));
    if (!gpu.id.valid()) gpu.id = GpuId(static_cast<std::uint64_t>(i + 1));
    gpu.generation = GpuGeneration(1);
    gpu.name = (name[0] != '\0') ? std::string(name) : ("gpu" + std::to_string(i));
    gpu.pci = PciAddress(static_cast<std::uint16_t>(pci.domain),
                         static_cast<std::uint8_t>(pci.bus & 0xFF),
                         static_cast<std::uint8_t>(pci.device & 0x1F),
                         static_cast<std::uint8_t>(fn & 0x7));
    gpu.cuda_device = DeviceId(static_cast<std::uint64_t>(i));
    gpu.cuda_verified = true;  // NVML proves an NVIDIA device is present
    unsigned int node = 0;
    if (a.getNumaNode != nullptr && a.getNumaNode(dev, &node) == 0 && node < 0x00FFFFFFu) {
      gpu.numa_node = NumaNodeId(static_cast<std::uint64_t>(node) + 1);
    }
    gpu.provenance = Provenance(EvidenceKind::kReal, {Source::kNvml});
    gpu.freshness = ctx.freshness;
    out.gpus.push_back(std::move(gpu));
  }
  a.shutdown();
  ::FreeLibrary(static_cast<HMODULE>(a.handle));
  return out;
}

}  // namespace nicresidency
