// SystemBackend: REAL host discovery on Windows using SetupAPI and the IP
// Helper API.  Cell: nothing is simulated; genuinely unobservable capabilities are
// left UNSUPPORTED (never inferred from a name or link speed).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "nicresidency/system_backend.hpp"

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devguid.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace nicresidency {

namespace {

std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int size = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                   nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), char(0));
  ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], size,
                        nullptr, nullptr);
  return out;
}

// Parse "PCI bus X, device Y, function Z" (SPDRP_LOCATION_INFORMATION).
bool parse_location(const std::wstring& raw, PciAddress& out) {
  if (raw.empty()) return false;
  if (raw.find(L"PCI") == std::wstring::npos) return false;
  unsigned bus = 0, dev = 0, fn = 0;
  if (std::swscanf(raw.c_str(), L"PCI bus %u, device %u, function %u", &bus, &dev, &fn) == 3) {
    out = PciAddress(0, static_cast<std::uint8_t>(bus & 0xFF),
                     static_cast<std::uint8_t>(dev & 0x1F),
                     static_cast<std::uint8_t>(fn & 0x7));
    return true;
  }
  return false;
}

std::wstring reg_prop_string(HDEVINFO hdev, PSP_DEVINFO_DATA data, DWORD prop) {
  wchar_t buffer[1024] = {0};
  DWORD type = 0, size = 0;
  if (::SetupDiGetDeviceRegistryPropertyW(hdev, data, prop, &type,
                                          reinterpret_cast<BYTE*>(buffer),
                                          static_cast<DWORD>(sizeof(buffer)), &size)) {
    if (size >= sizeof(wchar_t)) {
      buffer[size / sizeof(wchar_t) - 1] = L'\0';  // ensure NUL
      return std::wstring(buffer);
    }
  }
  return std::wstring();
}

}  // namespace

DiscoveryResult SystemBackend::discover(const DiscoveryContext& ctx) const {
  DiscoveryResult out;
  out.notes.push_back("windows-system backend: REAL host enumeration");

  // Map adapter friendly names -> operational status.
  std::vector<BYTE> buf(16 * 1024);
  ULONG bufsize = static_cast<ULONG>(buf.size());
  ULONG rc = ::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                                   reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufsize);
  if (rc != NO_ERROR) {
    out.notes.push_back("GetAdaptersAddresses failed; link state UNKNOWN");
  }

  auto link_of = [&](const std::wstring& friendly) -> LinkState {
    auto* aa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    for (; aa != nullptr; aa = aa->Next) {
      if (aa->FriendlyName != nullptr && friendly == aa->FriendlyName) {
        return (aa->OperStatus == IfOperStatusUp) ? LinkState::kUp : LinkState::kDown;
      }
    }
    return LinkState::kUnknown;
  };

  HDEVINFO hdev = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_NET, nullptr, nullptr, DIGCF_PRESENT);
  if (hdev == INVALID_HANDLE_VALUE) {
    throw ResidencyError(ErrorCode::kBackendUnavailable,
                         "SetupDiGetClassDevs(net) failed to enumerate NICs");
  }

  SP_DEVINFO_DATA data;
  data.cbSize = sizeof(data);
  for (DWORD i = 0; ::SetupDiEnumDeviceInfo(hdev, i, &data); ++i) {
    if (out.nics.size() >= ctx.max_nics) {
      out.notes.push_back("NIC enumeration truncated at max_nics");
      break;
    }
    std::wstring desc = reg_prop_string(hdev, &data, SPDRP_DEVICEDESC);
    std::wstring friendly = reg_prop_string(hdev, &data, SPDRP_FRIENDLYNAME);
    if (friendly.empty()) friendly = desc;
    std::wstring location = reg_prop_string(hdev, &data, SPDRP_LOCATION_INFORMATION);
    std::wstring hw_id = reg_prop_string(hdev, &data, SPDRP_HARDWAREID);

    // Only consider real PCI NICs (present + on a PCI bus).
    bool is_pci = hw_id.size() >= 4 && hw_id[3] == L'\\';
    if (!is_pci || hw_id.size() < 5) continue;  // Wi-Fi / Bluetooth etc.

    NicRecord nic;
    nic.pci = PciAddress();
    if (!parse_location(location, nic.pci)) {
      out.notes.push_back("one NIC had no parseable PCI location; skipped");
      continue;
    }
    unsigned ven = 0, dev = 0, subsys = 0, rev = 0;
    std::swscanf(hw_id.c_str(), L"PCI\\VEN_%x&DEV_%x&SUBSYS_%x&REV_%x", &ven, &dev, &subsys, &rev);
    nic.vendor_id = static_cast<std::uint16_t>(ven & 0xFFFF);
    nic.device_id = static_cast<std::uint16_t>(dev & 0xFFFF);
    nic.subsystem_id = static_cast<std::uint16_t>(subsys & 0xFFFF);
    nic.subsystem_vendor_id = static_cast<std::uint16_t>((subsys >> 16) & 0xFFFF);
    nic.revision = static_cast<std::uint8_t>(rev & 0xFF);

    // Stable logical id derived from the PCI BDF (never a handle).
    nic.id = NicId(static_cast<std::uint64_t>(nic.pci.numeric()));
    nic.generation = NicGeneration(1);
    nic.os_name = to_utf8(friendly.empty() ? desc : friendly);
    nic.os_description = to_utf8(desc);
    nic.device_kind = DeviceKind::kNic;
    nic.function_class = FunctionClass::kUnknown;
    nic.capability.set_generation(CapabilityGeneration(1));
    nic.capability.set_accelerator_affinity_supported(false);
    nic.capability.set_provenance(Provenance(EvidenceKind::kReal,{Source::kNetAdapterApi, Source::kPci}));
    // Per-device NUMA is not exposed by a documented DEVPROPKEY in this
    // SDK; leave NIC NUMA UNKNOWN and derive NIC<->GPU locality from the
    // PCI hierarchy (real BDFs) and real NVML GPU NUMA when available.
    nic.numa_node = NumaNodeId();
    nic.link_state = link_of(friendly);
    nic.health = HealthState::kReady;
    nic.lifecycle = LifecycleState::kAvailable;
    nic.provenance = Provenance(EvidenceKind::kReal,{Source::kOperatingSystem, Source::kNetAdapterApi});
    nic.freshness = ctx.freshness;
    out.nics.push_back(std::move(nic));
  }
  DWORD err = ::GetLastError();
  if (err != ERROR_NO_MORE_ITEMS && err != ERROR_SUCCESS && out.nics.empty()) {
    ::SetupDiDestroyDeviceInfoList(hdev);
    throw ResidencyError(ErrorCode::kBackendError, "SetupAPI NIC enumeration error");
  }
  ::SetupDiDestroyDeviceInfoList(hdev);

  out.topology = build_topology(ctx.topology_generation, ctx.max_topology_nodes);
  if (out.topology == nullptr) out.notes.push_back("no PCI topology available");
  return out;
}

std::unique_ptr<TopologySnapshot> SystemBackend::build_topology(
    TopologyGeneration gen, std::size_t max_nodes) const {
  // Group all PCI devices by (segment, bus).  Each device node carries REAL
  // PCI provenance for its own BDF; the grouping into root complexes /
  // switches is a DERIVED construction.
  HDEVINFO hdev = ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT);
  if (hdev == INVALID_HANDLE_VALUE) return nullptr;

  TopologyBuilder b;
  SP_DEVINFO_DATA data;
  data.cbSize = sizeof(data);
  std::vector<PciAddress> addrs;
  for (DWORD i = 0; ::SetupDiEnumDeviceInfo(hdev, i, &data); ++i) {
    std::wstring location = reg_prop_string(hdev, &data, SPDRP_LOCATION_INFORMATION);
    std::wstring hw_id = reg_prop_string(hdev, &data, SPDRP_HARDWAREID);
    if (hw_id.size() < 5) continue;
    if (!(hw_id.size() >= 4 && hw_id[3] == L'\\')) continue;
    PciAddress pci;
    if (!parse_location(location, pci)) continue;
    addrs.push_back(pci);
    if (b.node_count() >= max_nodes) break;
  }
  ::SetupDiDestroyDeviceInfoList(hdev);

  std::map<std::uint32_t, PciNodeId> bus_switch;
  std::map<std::uint16_t, PciNodeId> seg_rc;
  std::uint64_t nc = 1;
  for (const auto& pci : addrs) {
    std::uint16_t seg = pci.segment();
    std::uint32_t bus = pci.bus();
    PciNodeId sw;
    auto bit = bus_switch.find(bus);
    if (bit == bus_switch.end()) {
      sw = PciNodeId(nc++);
      bus_switch[bus] = sw;
      Provenance pv(EvidenceKind::kReal,{Source::kPci, Source::kDerived});
      b.add_node(TopologyNode(sw, PciAddress(seg, static_cast<std::uint8_t>(bus), 0, 0),
                          TopologyNodeKind::kSwitch, PciNodeId(), NumaNodeId(), pv));
    } else sw = bit->second;
    PciNodeId rc;
    auto rit = seg_rc.find(seg);
    if (rit == seg_rc.end()) {
      rc = PciNodeId(nc++);
      seg_rc[seg] = rc;
      Provenance pv(EvidenceKind::kReal,{Source::kPci, Source::kDerived});
      b.add_node(TopologyNode(rc, PciAddress(seg, 0, 0, 0), TopologyNodeKind::kRootComplex,
                          PciNodeId(), NumaNodeId(), pv));
    } else rc = rit->second;
    b.add_edge(sw, rc);
    Provenance pv(EvidenceKind::kReal,{Source::kPci});
    PciNodeId dev = PciNodeId(nc++);
    b.add_node(TopologyNode(dev, pci, TopologyNodeKind::kDevice, sw, NumaNodeId(), pv));
    b.add_edge(dev, sw);
  }

  return std::make_unique<TopologySnapshot>(b.build(gen));
}

}  // namespace nicresidency
