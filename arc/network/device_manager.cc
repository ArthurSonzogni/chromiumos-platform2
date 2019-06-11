// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device_manager.h"

#include <utility>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace {
constexpr const char kVpnInterfaceHostPattern[] = "tun";
constexpr const char kVpnInterfaceGuestPrefix[] = "cros_";
constexpr const char kEthernetInterfacePrefix[] = "eth";
constexpr std::array<const char*, 2> kWifiInterfacePrefixes{{"wlan", "mlan"}};

bool IsHostVpnInterface(const std::string& ifname) {
  return base::StartsWith(ifname, kVpnInterfaceHostPattern,
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool IsEthernetInterface(const std::string& ifname) {
  return base::StartsWith(ifname, kEthernetInterfacePrefix,
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool IsWifiInterface(const std::string& ifname) {
  for (const auto& prefix : kWifiInterfacePrefixes) {
    if (base::StartsWith(ifname, prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}
}  // namespace

namespace arc_networkd {

DeviceManager::DeviceManager(AddressManager* addr_mgr,
                             const Device::MessageSink& msg_sink,
                             const std::string& arc_device)
    : addr_mgr_(addr_mgr), msg_sink_(msg_sink) {
  DCHECK(addr_mgr_);
  Add(arc_device);
}

DeviceManager::~DeviceManager() {}

size_t DeviceManager::Reset(const std::set<std::string>& devices) {
  for (auto it = devices_.begin(); it != devices_.end();) {
    const std::string& name = it->first;
    if (name != kAndroidDevice && name != kAndroidLegacyDevice &&
        devices.find(name) == devices.end()) {
      LOG(INFO) << "Removing device " << name;
      it = devices_.erase(it);
    } else {
      ++it;
    }
  }
  for (const std::string& name : devices) {
    Add(name);
  }
  return devices_.size();
}

bool DeviceManager::Add(const std::string& name) {
  if (name.empty() || devices_.find(name) != devices_.end())
    return false;

  auto device = MakeDevice(name);
  if (!device)
    return false;

  LOG(INFO) << "Adding device " << *device;
  devices_.emplace(name, std::move(device));
  return true;
}

bool DeviceManager::EnableLegacyDevice(const std::string& ifname) {
  const auto it = devices_.find(kAndroidLegacyDevice);
  if (it == devices_.end()) {
    LOG(WARNING) << "Enable not supported in multinetworking mode";
    return false;
  }

  it->second->Disable();
  if (!ifname.empty())
    it->second->Enable(ifname);
  return true;
}

bool DeviceManager::DisableLegacyDevice() {
  return EnableLegacyDevice("");
}

void DeviceManager::EnableAllDevices() {
  for (auto const& kv : devices_) {
    if (kv.first == kAndroidDevice) {
      continue;
    }
    kv.second->Enable(kv.first);
  }
}

void DeviceManager::DisableAllDevices() {
  for (auto const& kv : devices_) {
    if (kv.first == kAndroidDevice) {
      continue;
    }
    kv.second->Disable();
  }
}

std::unique_ptr<Device> DeviceManager::MakeDevice(
    const std::string& name) const {
  DCHECK(!name.empty());

  Device::Options opts;
  std::string host_ifname, guest_ifname;
  AddressManager::Guest guest = AddressManager::Guest::ARC;

  if (name == kAndroidLegacyDevice) {
    host_ifname = "arcbr0";
    guest_ifname = "arc0";
    opts.find_ipv6_routes = true;
    opts.fwd_multicast = true;
  } else {
    if (name == kAndroidDevice) {
      host_ifname = "arcbr0";
    } else {
      guest = AddressManager::Guest::ARC_NET;
      host_ifname = base::StringPrintf("arc_%s", name.c_str());
    }
    guest_ifname = name;
    // Android VPNs and native VPNs use the same "tun%d" name pattern for VPN
    // tun interfaces. To distinguish between both and avoid name collisions
    // native VPN interfaces are not exposed with their exact names inside the
    // ARC network namespace. This additional naming convention is not known to
    // Chrome and ARC has to fix names in ArcNetworkBridge.java when receiving
    // NetworkConfiguration mojo objects from Chrome.
    if (IsHostVpnInterface(guest_ifname)) {
      guest_ifname = kVpnInterfaceGuestPrefix + guest_ifname;
    }
    // TODO(crbug/726815) Also enable |find_ipv6_routes| for cellular networks
    // once IPv6 is enabled on cellular networks in shill.
    opts.find_ipv6_routes =
        IsEthernetInterface(guest_ifname) || IsWifiInterface(guest_ifname);
    opts.fwd_multicast =
        IsEthernetInterface(guest_ifname) || IsWifiInterface(guest_ifname);
  }

  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(guest);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable. Cannot make device: "
               << name;
    return nullptr;
  }
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR)
        << "Bridge address already in use or unavailable. Cannot make device: "
        << name;
    return nullptr;
  }
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR)
        << "ARC address already in use or unavailable. Cannot make device: "
        << name;
    return nullptr;
  }

  auto config = std::make_unique<Device::Config>(
      host_ifname, guest_ifname, addr_mgr_->GenerateMacAddress(),
      std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));

  return std::make_unique<Device>(name, std::move(config), opts, msg_sink_);
}

}  // namespace arc_networkd
