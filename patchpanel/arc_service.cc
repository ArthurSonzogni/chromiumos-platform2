// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/utsname.h>

#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/manager.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
// UID of Android root, relative to the host pid namespace.
const int32_t kAndroidRootUid = 655360;
constexpr uint32_t kInvalidId = 0;
constexpr char kArcNetnsName[] = "arc_netns";
constexpr char kArcIfname[] = "arc0";

bool IsAdbAllowed(ShillClient::Device::Type type) {
  static const std::set<ShillClient::Device::Type> adb_allowed_types{
      ShillClient::Device::Type::kEthernet,
      ShillClient::Device::Type::kEthernetEap,
      ShillClient::Device::Type::kWifi,
  };
  return adb_allowed_types.find(type) != adb_allowed_types.end();
}

bool KernelVersion(int* major, int* minor) {
  struct utsname u;
  if (uname(&u) != 0) {
    PLOG(ERROR) << "uname failed";
    *major = *minor = 0;
    return false;
  }
  int unused;
  if (sscanf(u.release, "%d.%d.%d", major, minor, &unused) != 3) {
    LOG(ERROR) << "unexpected release string: " << u.release;
    *major = *minor = 0;
    return false;
  }
  return true;
}

// Makes Android root the owner of /sys/class/ + |path|. |pid| is the ARC
// container pid.
void SetSysfsOwnerToAndroidRoot(uint32_t pid, const std::string& path) {
  auto ns = ScopedNS::EnterMountNS(pid);
  if (!ns) {
    LOG(ERROR) << "Cannot enter mnt namespace for pid " << pid;
    return;
  }

  const std::string sysfs_path = "/sys/class/" + path;
  if (chown(sysfs_path.c_str(), kAndroidRootUid, kAndroidRootUid) == -1)
    PLOG(ERROR) << "Failed to change ownership of " + sysfs_path;
}

void OneTimeContainerSetup(const Datapath& datapath, uint32_t pid) {
  static bool done = false;
  if (done)
    return;

  auto& runner = datapath.runner();

  // Load networking modules needed by Android that are not compiled in the
  // kernel. Android does not allow auto-loading of kernel modules.
  // Expected for all kernels.
  if (runner.modprobe_all({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
      }) != 0) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
  }
  // The xfrm modules needed for Android's ipsec APIs on kernels < 5.4.
  int major, minor;
  if (KernelVersion(&major, &minor) &&
      (major < 5 || (major == 5 && minor < 4)) &&
      runner.modprobe_all({
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
      }) != 0) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
  }

  // Optional modules.
  if (runner.modprobe_all({
          // This module is not available in kernels < 3.18
          "nf_reject_ipv6",
          // These modules are needed for supporting Chrome traffic on Android
          // VPN which uses Android's NAT feature. Android NAT sets up
          // iptables
          // rules that use these conntrack modules for FTP/TFTP.
          "nf_nat_ftp",
          "nf_nat_tftp",
          // The tun module is needed by the Android 464xlat clatd process.
          "tun",
      }) != 0) {
    LOG(WARNING) << "One or more optional kernel modules failed to load.";
  }

  // This is only needed for CTS (b/27932574).
  SetSysfsOwnerToAndroidRoot(pid, "xt_idletimer");

  done = true;
}

// Returns the ARC management device used for VPN forwarding, ADB-over-TCP.
std::unique_ptr<Device> MakeArcDevice(AddressManager* addr_mgr,
                                      GuestMessage::GuestType guest) {
  auto ipv4_subnet = addr_mgr->AllocateIPv4Subnet(AddressManager::Guest::ARC);
  if (!ipv4_subnet) {
    LOG(ERROR) << "Subnet already in use or unavailable";
    return nullptr;
  }

  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  if (!host_ipv4_addr) {
    LOG(ERROR) << "Bridge address already in use or unavailable";
    return nullptr;
  }

  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  if (!guest_ipv4_addr) {
    LOG(ERROR) << "ARC address already in use or unavailable";
    return nullptr;
  }

  int subnet_index = (guest == GuestMessage::ARC_VM) ? 1 : kAnySubnetIndex;

  auto config = std::make_unique<Device::Config>(
      addr_mgr->GenerateMacAddress(subnet_index), std::move(ipv4_subnet),
      std::move(host_ipv4_addr), std::move(guest_ipv4_addr));

  return std::make_unique<Device>(kArcIfname, kArcBridge, kArcIfname,
                                  std::move(config));
}
}  // namespace

ArcService::ArcService(ShillClient* shill_client,
                       Datapath* datapath,
                       AddressManager* addr_mgr,
                       GuestMessage::GuestType guest,
                       Device::ChangeEventHandler device_changed_handler)
    : shill_client_(shill_client),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      guest_(guest),
      device_changed_handler_(device_changed_handler),
      id_(kInvalidId) {
  arc_device_ = MakeArcDevice(addr_mgr, guest_);
  AllocateAddressConfigs();
  shill_client_->RegisterDevicesChangedHandler(
      base::Bind(&ArcService::OnDevicesChanged, weak_factory_.GetWeakPtr()));
  shill_client_->ScanDevices();
}

ArcService::~ArcService() {
  if (IsStarted()) {
    Stop(id_);
  }
}

bool ArcService::IsStarted() const {
  return id_ != kInvalidId;
}

void ArcService::AllocateAddressConfigs() {
  // The first usable subnet is the "other" ARC device subnet.
  // As a temporary workaround, for ARCVM, allocate fixed MAC addresses.
  uint8_t mac_addr_index = 2;
  // Allocate 2 subnets each for Ethernet and WiFi and 1 for LTE WAN interfaces.
  for (const auto type :
       {ShillClient::Device::Type::kEthernet,
        ShillClient::Device::Type::kEthernet, ShillClient::Device::Type::kWifi,
        ShillClient::Device::Type::kWifi,
        ShillClient::Device::Type::kCellular}) {
    auto ipv4_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::ARC_NET);
    if (!ipv4_subnet) {
      LOG(ERROR) << "Subnet already in use or unavailable";
      continue;
    }
    // For here out, use the same slices.
    auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
    if (!host_ipv4_addr) {
      LOG(ERROR) << "Bridge address already in use or unavailable";
      continue;
    }
    auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
    if (!guest_ipv4_addr) {
      LOG(ERROR) << "ARC address already in use or unavailable";
      continue;
    }

    MacAddress mac_addr = (guest_ == GuestMessage::ARC_VM)
                              ? addr_mgr_->GenerateMacAddress(mac_addr_index++)
                              : addr_mgr_->GenerateMacAddress();
    available_configs_[type].emplace_back(std::make_unique<Device::Config>(
        mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
        std::move(guest_ipv4_addr)));
  }

  all_configs_.push_back(&arc_device_->config());
  // Iterate over |available_configs_| with a fixed explicit order and do not
  // rely on the implicit ordering derived from key values.
  for (const auto type :
       {ShillClient::Device::Type::kEthernet, ShillClient::Device::Type::kWifi,
        ShillClient::Device::Type::kCellular}) {
    for (const auto& c : available_configs_[type]) {
      all_configs_.push_back(c.get());
    }
  }
}

std::unique_ptr<Device::Config> ArcService::AcquireConfig(
    ShillClient::Device::Type type) {
  // Normalize shill Device types for different ethernet flavors.
  if (type == ShillClient::Device::Type::kEthernetEap)
    type = ShillClient::Device::Type::kEthernet;

  auto it = available_configs_.find(type);
  if (it == available_configs_.end()) {
    LOG(ERROR) << "Unsupported shill Device type " << type;
    return nullptr;
  }

  if (it->second.empty()) {
    LOG(ERROR)
        << "Cannot make virtual device: No more addresses available for type "
        << type;
    return nullptr;
  }

  std::unique_ptr<Device::Config> config;
  config = std::move(it->second.front());
  it->second.pop_front();
  return config;
}

void ArcService::ReleaseConfig(ShillClient::Device::Type type,
                               std::unique_ptr<Device::Config> config) {
  available_configs_[type].push_front(std::move(config));
}

bool ArcService::Start(uint32_t id) {
  if (IsStarted()) {
    LOG(WARNING) << "Already running - did something crash?"
                 << " Stopping and restarting...";
    Stop(id_);
  }

  std::string arc_device_ifname;
  if (guest_ == GuestMessage::ARC_VM) {
    // Allocate TAP devices for all configs.
    for (auto* config : all_configs_) {
      auto mac = config->mac_addr();
      auto tap = datapath_->AddTAP("" /* auto-generate name */, &mac,
                                   nullptr /* no ipv4 subnet */,
                                   vm_tools::kCrosVmUser);
      if (tap.empty()) {
        LOG(ERROR) << "Failed to create TAP device";
        continue;
      }

      config->set_tap_ifname(tap);
    }
    arc_device_ifname = arc_device_->config().tap_ifname();
  } else {
    OneTimeContainerSetup(*datapath_, id);
    if (!datapath_->NetnsAttachName(kArcNetnsName, id)) {
      LOG(ERROR) << "Failed to attach name " << kArcNetnsName << " to pid "
                 << id;
      return false;
    }
    arc_device_ifname = ArcVethHostName(arc_device_->guest_ifname());
    if (!datapath_->ConnectVethPair(id, kArcNetnsName, arc_device_ifname,
                                    arc_device_->guest_ifname(),
                                    arc_device_->config().mac_addr(),
                                    arc_device_->config().guest_ipv4_addr(), 30,
                                    false /*remote_multicast_flag*/)) {
      LOG(ERROR) << "Cannot create virtual link for device "
                 << arc_device_->phys_ifname();
      return false;
    }
    // Allow netd to write to /sys/class/net/arc0/mtu (b/175571457).
    SetSysfsOwnerToAndroidRoot(id,
                               "net/" + arc_device_->guest_ifname() + "/mtu");
  }
  id_ = id;

  // Create the bridge for the management device arc0.
  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge when the service stops, bring down the
  // device instead and re-up it on restart.
  if (!datapath_->AddBridge(kArcBridge, arc_device_->config().host_ipv4_addr(),
                            30) &&
      !datapath_->MaskInterfaceFlags(kArcBridge, IFF_UP)) {
    LOG(ERROR) << "Failed to bring up arc bridge " << kArcBridge;
    return false;
  }

  if (!datapath_->AddToBridge(kArcBridge, arc_device_ifname)) {
    LOG(ERROR) << "Failed to bridge arc device " << arc_device_ifname << " to "
               << kArcBridge;
    return false;
  }
  LOG(INFO) << "Started ARC management device " << *arc_device_.get();

  // Start already known Shill <-> ARC mapped devices.
  for (const auto& [ifname, type] : shill_devices_)
    AddDevice(ifname, type);

  // Enable conntrack helpers (b/172214190).
  if (!datapath_->SetConntrackHelpers(true)) {
    LOG(ERROR) << "Failed to enable conntrack helpers";
    return false;
  }

  return true;
}

void ArcService::Stop(uint32_t id) {
  if (!IsStarted()) {
    LOG(ERROR) << "ArcService was not running";
    return;
  }

  // After the ARC container has stopped, the pid is not known anymore.
  if (guest_ == GuestMessage::ARC_VM && id_ != id) {
    LOG(ERROR) << "Mismatched ARCVM CIDs " << id_ << " != " << id;
    return;
  }

  if (!datapath_->SetConntrackHelpers(false))
    LOG(ERROR) << "Failed to disable conntrack helpers";

  // Stop Shill <-> ARC mapped devices.
  for (const auto& [ifname, type] : shill_devices_)
    RemoveDevice(ifname, type);

  // Per crbug/1008686 this device cannot be deleted and then re-added.
  // So instead of removing the bridge, bring it down and mark it. This will
  // allow us to detect if the device is re-added in case of a crash restart
  // and do the right thing.
  if (!datapath_->MaskInterfaceFlags(kArcBridge, IFF_DEBUG, IFF_UP))
    LOG(ERROR) << "Failed to bring down arc bridge "
               << "- it may not restart correctly";

  if (guest_ == GuestMessage::ARC) {
    datapath_->RemoveInterface(ArcVethHostName(arc_device_->phys_ifname()));
    if (!datapath_->NetnsDeleteName(kArcNetnsName))
      LOG(WARNING) << "Failed to delete netns name " << kArcNetnsName;
  }

  // Destroy allocated TAP devices if any, including the ARC management device.
  for (auto* config : all_configs_) {
    if (config->tap_ifname().empty())
      continue;
    datapath_->RemoveInterface(config->tap_ifname());
    config->set_tap_ifname("");
  }

  LOG(INFO) << "Stopped ARC management device " << *arc_device_.get();
  id_ = kInvalidId;
}

void ArcService::OnDevicesChanged(const std::set<std::string>& added,
                                  const std::set<std::string>& removed) {
  for (const std::string& ifname : removed) {
    RemoveDevice(ifname, shill_devices_[ifname]);
    shill_devices_.erase(ifname);
  }

  for (const std::string& ifname : added) {
    ShillClient::Device shill_device;
    if (!shill_client_->GetDeviceProperties(ifname, &shill_device)) {
      LOG(ERROR) << "Could not read shill Device properties for " << ifname;
      continue;
    }
    shill_devices_[ifname] = shill_device.type;
    AddDevice(ifname, shill_device.type);
  }
}

void ArcService::AddDevice(const std::string& ifname,
                           ShillClient::Device::Type type) {
  if (!IsStarted())
    return;

  if (ifname.empty())
    return;

  if (devices_.find(ifname) != devices_.end()) {
    LOG(DFATAL) << "Attemping to add already tracked device: " << ifname;
    return;
  }

  auto config = AcquireConfig(type);
  if (!config) {
    LOG(ERROR) << "Cannot acquire a Config for " << ifname;
    return;
  }

  auto device = std::make_unique<Device>(ifname, ArcBridgeName(ifname), ifname,
                                         std::move(config));
  LOG(INFO) << "Starting device " << *device;

  // Create the bridge.
  if (!datapath_->AddBridge(device->host_ifname(),
                            device->config().host_ipv4_addr(), 30)) {
    LOG(ERROR) << "Failed to setup bridge " << device->host_ifname();
    return;
  }

  datapath_->StartRoutingDevice(device->phys_ifname(), device->host_ifname(),
                                device->config().guest_ipv4_addr(),
                                TrafficSource::ARC, false /*route_on_vpn*/);

  std::string virtual_device_ifname;
  if (guest_ == GuestMessage::ARC_VM) {
    virtual_device_ifname = device->config().tap_ifname();
    if (virtual_device_ifname.empty()) {
      LOG(ERROR) << "No TAP device for " << *device;
      return;
    }
  } else {
    virtual_device_ifname = ArcVethHostName(device->guest_ifname());
    if (!datapath_->ConnectVethPair(
            id_, kArcNetnsName, virtual_device_ifname, device->guest_ifname(),
            device->config().mac_addr(), device->config().guest_ipv4_addr(), 30,
            IsMulticastInterface(device->phys_ifname()))) {
      LOG(ERROR) << "Cannot create veth link for device " << *device;
      return;
    }
    // Allow netd to write to /sys/class/net/<guest_ifname>/mtu (b/169936104).
    SetSysfsOwnerToAndroidRoot(id_, "net/" + device->guest_ifname() + "/mtu");
  }

  if (!datapath_->AddToBridge(device->host_ifname(), virtual_device_ifname)) {
    if (guest_ == GuestMessage::ARC) {
      datapath_->RemoveInterface(virtual_device_ifname);
    }
    LOG(ERROR) << "Failed to bridge interface " << virtual_device_ifname;
    return;
  }

  if (IsAdbAllowed(type) && !datapath_->AddAdbPortAccessRule(ifname)) {
    LOG(ERROR) << "Failed to add ADB port access rule";
  }

  device_changed_handler_.Run(*device, Device::ChangeEvent::ADDED, guest_);
  devices_.emplace(ifname, std::move(device));
}

void ArcService::RemoveDevice(const std::string& ifname,
                              ShillClient::Device::Type type) {
  if (!IsStarted())
    return;

  const auto it = devices_.find(ifname);
  if (it == devices_.end()) {
    LOG(WARNING) << "Unknown device: " << ifname;
    return;
  }

  const auto* device = it->second.get();
  LOG(INFO) << "Removing device " << *device;

  device_changed_handler_.Run(*device, Device::ChangeEvent::REMOVED, guest_);

  // ARCVM TAP devices are removed in VmImpl::Stop() when the service stops
  if (guest_ == GuestMessage::ARC)
    datapath_->RemoveInterface(ArcVethHostName(device->phys_ifname()));

  datapath_->StopRoutingDevice(device->phys_ifname(), device->host_ifname(),
                               device->config().guest_ipv4_addr(),
                               TrafficSource::ARC, false /*route_on_vpn*/);
  datapath_->RemoveBridge(device->host_ifname());
  if (IsAdbAllowed(type))
    datapath_->DeleteAdbPortAccessRule(ifname);

  // Once the physical Device is gone it may not be possible to retrieve
  // the device type from Shill DBus interface by interface name.
  ReleaseConfig(type, it->second->release_config());
  devices_.erase(it);
}

std::vector<const Device::Config*> ArcService::GetDeviceConfigs() const {
  std::vector<const Device::Config*> configs;
  for (auto* c : all_configs_)
    configs.emplace_back(c);

  return configs;
}

void ArcService::ScanDevices(
    base::RepeatingCallback<void(const Device&)> callback) const {
  for (const auto& [_, d] : devices_)
    callback.Run(*d.get());
}

}  // namespace patchpanel
