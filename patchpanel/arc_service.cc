// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <optional>
#include <string_view>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/metrics.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"
#include "patchpanel/patchpanel_daemon.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/scoped_ns.h"

namespace patchpanel {
namespace {
// UID of Android root, relative to the host pid namespace.
const int32_t kAndroidRootUid = 655360;
constexpr uint32_t kInvalidId = 0;
constexpr char kArcNetnsName[] = "arc_netns";
constexpr char kArcVmIfnamePrefix[] = "eth";

void RecordEvent(MetricsLibraryInterface* metrics, ArcServiceUmaEvent event) {
  metrics->SendEnumToUMA(kArcServiceUmaEventMetrics, event);
}

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
bool SetSysfsOwnerToAndroidRoot(pid_t pid, const std::string& path) {
  auto ns = ScopedNS::EnterMountNS(pid);
  if (!ns) {
    LOG(ERROR) << "Cannot enter mnt namespace for pid " << pid;
    return false;
  }

  const std::string sysfs_path = "/sys/class/" + path;
  if (chown(sysfs_path.c_str(), kAndroidRootUid, kAndroidRootUid) == -1) {
    PLOG(ERROR) << "Failed to change ownership of " + sysfs_path;
    return false;
  }

  return true;
}

bool OneTimeContainerSetup(Datapath& datapath, pid_t pid) {
  static bool done = false;
  if (done)
    return true;

  bool success = true;

  // Load networking modules needed by Android that are not compiled in the
  // kernel. Android does not allow auto-loading of kernel modules.
  // Expected for all kernels.
  if (!datapath.ModprobeAll({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
      })) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
    success = false;
  }
  // The xfrm modules needed for Android's ipsec APIs on kernels < 5.4.
  int major, minor;
  if (KernelVersion(&major, &minor) &&
      (major < 5 || (major == 5 && minor < 4)) &&
      !datapath.ModprobeAll({
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
      })) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
    success = false;
  }

  // Additional modules optional for CTS compliance but required for some
  // Android features.
  if (!datapath.ModprobeAll({
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
      })) {
    LOG(WARNING) << "One or more optional kernel modules failed to load.";
    success = false;
  }

  // This is only needed for CTS (b/27932574).
  if (!SetSysfsOwnerToAndroidRoot(pid, "xt_idletimer")) {
    success = false;
  }

  done = true;
  return success;
}

std::string PrefixIfname(std::string_view prefix, std::string_view ifname) {
  std::string n;
  n.append(prefix);
  n.append(ifname);
  if (n.length() >= IFNAMSIZ) {
    n.resize(IFNAMSIZ - 1);
    // Best effort attempt to preserve the interface number, assuming it's the
    // last char in the name.
    n.back() = ifname.back();
  }
  return n;
}
}  // namespace

ArcService::ArcConfig::ArcConfig(const MacAddress& mac_addr,
                                 std::unique_ptr<Subnet> ipv4_subnet)
    : mac_addr_(mac_addr), ipv4_subnet_(std::move(ipv4_subnet)) {}

ArcService::ArcDevice::ArcDevice(
    ArcType type,
    std::optional<std::string_view> shill_device_ifname,
    std::string_view arc_device_ifname,
    const MacAddress& arc_device_mac_address,
    const ArcConfig& arc_config,
    std::string_view bridge_ifname,
    std::string_view guest_device_ifname)
    : type_(type),
      shill_device_ifname_(shill_device_ifname),
      arc_device_ifname_(arc_device_ifname),
      arc_device_mac_address_(arc_device_mac_address),
      arc_ipv4_subnet_(arc_config.ipv4_subnet()),
      arc_ipv4_address_(arc_config.arc_ipv4_address()),
      bridge_ipv4_address_(arc_config.bridge_ipv4_address()),
      bridge_ifname_(bridge_ifname),
      guest_device_ifname_(guest_device_ifname) {}

ArcService::ArcDevice::~ArcDevice() {}

void ArcService::ArcDevice::ConvertToProto(NetworkDevice* output) const {
  // By convention, |phys_ifname| is set to "arc0" for the "arc0" device used
  // for VPN forwarding.
  output->set_phys_ifname(shill_device_ifname().value_or(kArc0Ifname));
  output->set_ifname(bridge_ifname());
  output->set_guest_ifname(guest_device_ifname());
  output->set_ipv4_addr(arc_ipv4_address().address().ToInAddr().s_addr);
  output->set_host_ipv4_addr(bridge_ipv4_address().address().ToInAddr().s_addr);
  switch (type()) {
    case ArcType::kContainer:
      output->set_guest_type(NetworkDevice::ARC);
      break;
    case ArcType::kVM:
      output->set_guest_type(NetworkDevice::ARCVM);
      break;
  }
  FillSubnetProto(arc_ipv4_subnet(), output->mutable_ipv4_subnet());
}

ArcService::ArcService(Datapath* datapath,
                       AddressManager* addr_mgr,
                       ArcType arc_type,
                       MetricsLibraryInterface* metrics,
                       ArcDeviceChangeHandler arc_device_change_handler)
    : datapath_(datapath),
      addr_mgr_(addr_mgr),
      arc_type_(arc_type),
      metrics_(metrics),
      arc_device_change_handler_(arc_device_change_handler),
      id_(kInvalidId) {
  AllocateArc0Config();
  AllocateAddressConfigs();
}

ArcService::~ArcService() {
  if (IsStarted()) {
    Stop(id_);
  }
}

bool ArcService::IsStarted() const {
  return id_ != kInvalidId;
}

// Creates the ARC management Device used for VPN forwarding, ADB-over-TCP.
void ArcService::AllocateArc0Config() {
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArc0);
  if (!ipv4_subnet) {
    LOG(ERROR) << __func__ << ": Subnet already in use or unavailable";
    return;
  }
  uint32_t subnet_index =
      (arc_type_ == ArcService::ArcType::kVM) ? 1 : kAnySubnetIndex;
  arc0_config_ = std::make_unique<ArcConfig>(
      addr_mgr_->GenerateMacAddress(subnet_index), std::move(ipv4_subnet));
  all_configs_.push_back(arc0_config_.get());
}

void ArcService::AllocateAddressConfigs() {
  // The first usable subnet is the "other" ARC Device subnet.
  // As a temporary workaround, for ARCVM, allocate fixed MAC addresses.
  uint32_t mac_addr_index = 2;
  // Allocate 2 subnets each for Ethernet and WiFi and 1 for LTE WAN interfaces.
  for (const auto type :
       {ShillClient::Device::Type::kEthernet,
        ShillClient::Device::Type::kEthernet, ShillClient::Device::Type::kWifi,
        ShillClient::Device::Type::kWifi,
        ShillClient::Device::Type::kCellular}) {
    auto ipv4_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArcNet);
    if (!ipv4_subnet) {
      LOG(ERROR) << __func__ << " " << type
                 << ": Subnet already in use or unavailable";
      continue;
    }
    MacAddress mac_addr = (arc_type_ == ArcType::kVM)
                              ? addr_mgr_->GenerateMacAddress(mac_addr_index++)
                              : addr_mgr_->GenerateMacAddress();
    const auto& config = available_configs_[type].emplace_back(
        std::make_unique<ArcConfig>(mac_addr, std::move(ipv4_subnet)));
    all_configs_.push_back(config.get());
  }
}

void ArcService::RefreshMacAddressesInConfigs() {
  for (auto* config : all_configs_) {
    config->set_mac_addr(addr_mgr_->GenerateMacAddress());
  }
}

std::unique_ptr<ArcService::ArcConfig> ArcService::AcquireConfig(
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
        << "Cannot make virtual Device: No more addresses available for type "
        << type;
    return nullptr;
  }

  std::unique_ptr<ArcConfig> config;
  config = std::move(it->second.front());
  it->second.pop_front();
  return config;
}

void ArcService::ReleaseConfig(ShillClient::Device::Type type,
                               std::unique_ptr<ArcConfig> config) {
  available_configs_[type].push_front(std::move(config));
}

bool ArcService::Start(uint32_t id) {
  RecordEvent(metrics_, ArcServiceUmaEvent::kStart);

  if (IsStarted()) {
    RecordEvent(metrics_, ArcServiceUmaEvent::kStartWithoutStop);
    LOG(WARNING) << "Already running - did something crash?"
                 << " Stopping and restarting...";
    Stop(id_);
  }

  std::string arc0_device_ifname;
  if (arc_type_ == ArcType::kVM) {
    // Allocate TAP devices for all configs.
    int arcvm_ifname_id = 0;
    for (auto* config : all_configs_) {
      // Tap device name is autogenerated. IPv4 is configured on the bridge.
      auto tap = datapath_->AddTunTap(/*name=*/"", config->mac_addr(),
                                      /*ipv4_addr=*/std::nullopt,
                                      vm_tools::kCrosVmUser, DeviceMode::kTap);
      if (tap.empty()) {
        LOG(ERROR) << "Failed to create TAP device";
        continue;
      }

      config->set_tap_ifname(tap);

      // Inside ARCVM, interface names follow the pattern eth%d (starting from
      // 0) following the order of the TAP interface.
      arcvm_guest_ifnames_[tap] =
          kArcVmIfnamePrefix + std::to_string(arcvm_ifname_id);
      arcvm_ifname_id++;
    }
    arc0_device_ifname = arc0_config_->tap_ifname();
  } else {
    pid_t pid = static_cast<int>(id);
    if (pid < 0) {
      LOG(ERROR) << "Invalid ARC container pid " << pid;
      return false;
    }
    if (!OneTimeContainerSetup(*datapath_, pid)) {
      RecordEvent(metrics_, ArcServiceUmaEvent::kOneTimeContainerSetupError);
      LOG(ERROR) << "One time container setup failed";
    }
    if (!datapath_->NetnsAttachName(kArcNetnsName, pid)) {
      LOG(ERROR) << "Failed to attach name " << kArcNetnsName << " to pid "
                 << pid;
      return false;
    }
    // b/208240700: Refresh MAC address in AddressConfigs every time ARC starts
    // to ensure ARC container has different MAC after optout and reopt-in.
    // TODO(b/185881882): this should be safe to remove after b/185881882.
    RefreshMacAddressesInConfigs();

    arc0_device_ifname = kVethArc0Ifname;
  }
  id_ = id;

  // The "arc0" virtual device is either attached on demand to host VPNs or is
  // used to forward host traffic into an Android VPN. Therefore, |shill_device|
  // is not meaningful for the "arc0" virtual device and is undefined.
  arc0_device_ = ArcDevice(arc_type_, std::nullopt, arc0_device_ifname,
                           arc0_config_->mac_addr(), *arc0_config_,
                           kArcbr0Ifname, kArc0Ifname);

  LOG(INFO) << "Starting ARC management Device " << *arc0_device_;
  StartArcDeviceDatapath(*arc0_device_);

  // Start already known shill <-> ARC mapped devices.
  for (const auto& [_, shill_device] : shill_devices_)
    AddDevice(shill_device);

  // Enable conntrack helpers needed for processing through SNAT the IPv4 GRE
  // packets sent by Android PPTP client (b/172214190).
  // TODO(b/252749921) Find alternative for chromeos-6.1+ kernels.
  if (!datapath_->SetConntrackHelpers(true)) {
    // Do not consider this error fatal for ARC datapath setup (b/252749921).
    LOG(ERROR) << "Failed to enable conntrack helpers";
  }

  RecordEvent(metrics_, ArcServiceUmaEvent::kStartSuccess);
  return true;
}

void ArcService::Stop(uint32_t id) {
  RecordEvent(metrics_, ArcServiceUmaEvent::kStop);
  if (!IsStarted()) {
    RecordEvent(metrics_, ArcServiceUmaEvent::kStopBeforeStart);
    LOG(ERROR) << "ArcService was not running";
    return;
  }

  // After the ARC container has stopped, the pid is not known anymore.
  // The stop message for ARCVM may be sent after a new VM is started. Only
  // stop if the CID matched the latest started ARCVM CID.
  if (arc_type_ == ArcType::kVM && id_ != id) {
    LOG(WARNING) << "Mismatched ARCVM CIDs " << id_ << " != " << id;
    return;
  }

  if (!datapath_->SetConntrackHelpers(false))
    LOG(ERROR) << "Failed to disable conntrack helpers";

  // Remove all ARC Devices associated with a shill Device.
  // Make a copy of |shill_devices_| to avoid invalidating any iterator over
  // |shill_devices_| while removing device from it and resetting it afterwards.
  auto shill_devices = shill_devices_;
  for (const auto& [_, shill_device] : shill_devices) {
    RemoveDevice(shill_device);
  }
  shill_devices_ = shill_devices;

  StopArcDeviceDatapath(*arc0_device_);
  LOG(INFO) << "Stopped ARC management Device " << *arc0_device_;
  arc0_device_ = std::nullopt;

  if (arc_type_ == ArcType::kVM) {
    // Destroy allocated TAP devices if any, including the ARC management
    // Device.
    for (auto* config : all_configs_) {
      if (config->tap_ifname().empty()) {
        continue;
      }
      datapath_->RemoveInterface(config->tap_ifname());
      config->set_tap_ifname("");
    }
    arcvm_guest_ifnames_.clear();
  } else {
    // Free the network namespace name attached to the ARC container.
    if (!datapath_->NetnsDeleteName(kArcNetnsName)) {
      LOG(ERROR) << "Failed to delete netns name " << kArcNetnsName;
    }
  }

  id_ = kInvalidId;
  RecordEvent(metrics_, ArcServiceUmaEvent::kStopSuccess);
}

void ArcService::AddDevice(const ShillClient::Device& shill_device) {
  shill_devices_[shill_device.shill_device_interface_property] = shill_device;
  if (!IsStarted())
    return;

  if (shill_device.ifname.empty())
    return;

  RecordEvent(metrics_, ArcServiceUmaEvent::kAddDevice);

  if (devices_.find(shill_device.ifname) != devices_.end()) {
    LOG(DFATAL) << "Attemping to add already tracked shill Device "
                << shill_device;
    return;
  }

  auto config = AcquireConfig(shill_device.type);
  if (!config) {
    LOG(ERROR) << "Cannot acquire an ARC IPv4 config for shill Device "
               << shill_device;
    return;
  }

  // The interface name visible inside ARC depends on the type of ARC
  // environment:
  //  - ARC container: the veth interface created inside ARC has the same name
  //  as the shill Device that this ARC virtual device is attached to.
  //  b/273741099: For Cellular multiplexed interfaces, the name of the shill
  //  Device is used such that the rest of the ARC stack does not need to be
  //  aware of Cellular multiplexing.
  //  - ARCVM: the interfaces created by virtio-net follow the pattern eth%d.
  auto guest_ifname = shill_device.shill_device_interface_property;
  if (arc_type_ == ArcType::kVM) {
    const auto it = arcvm_guest_ifnames_.find(config->tap_ifname());
    if (it == arcvm_guest_ifnames_.end()) {
      LOG(ERROR) << "Cannot acquire a ARCVM guest ifname for shill Device "
                 << shill_device;
    } else {
      guest_ifname = it->second;
    }
  }

  std::string arc_device_ifname;
  if (arc_type_ == ArcType::kVM) {
    arc_device_ifname = config->tap_ifname();
    if (arc_device_ifname.empty()) {
      LOG(ERROR) << "No TAP device for " << shill_device;
      return;
    }
  } else {
    arc_device_ifname = ArcVethHostName(shill_device);
  }

  auto arc_device_it = devices_.try_emplace(
      shill_device.ifname, arc_type_,
      shill_device.shill_device_interface_property, arc_device_ifname,
      config->mac_addr(), *config, ArcBridgeName(shill_device), guest_ifname);

  LOG(INFO) << "Starting ARC Device " << arc_device_it.first->second;
  StartArcDeviceDatapath(arc_device_it.first->second);

  arc_device_change_handler_.Run(shill_device, arc_device_it.first->second,
                                 ArcDeviceEvent::kAdded);
  assigned_configs_.emplace(shill_device.ifname, std::move(config));
  RecordEvent(metrics_, ArcServiceUmaEvent::kAddDeviceSuccess);
}

void ArcService::RemoveDevice(const ShillClient::Device& shill_device) {
  if (IsStarted()) {
    const auto it = devices_.find(shill_device.ifname);
    if (it == devices_.end()) {
      LOG(WARNING) << "Unknown shill Device " << shill_device;
    } else {
      const auto& arc_device = it->second;
      LOG(INFO) << "Removing ARC Device " << arc_device;
      arc_device_change_handler_.Run(shill_device, arc_device,
                                     ArcDeviceEvent::kRemoved);
      StopArcDeviceDatapath(arc_device);
      const auto config_it = assigned_configs_.find(shill_device.ifname);
      if (config_it == assigned_configs_.end()) {
        LOG(ERROR) << "No IPv4 configuration found for ARC Device "
                   << arc_device;
      } else {
        ReleaseConfig(shill_device.type, std::move(config_it->second));
        assigned_configs_.erase(config_it);
      }
      devices_.erase(it);
    }
  }
  shill_devices_.erase(shill_device.shill_device_interface_property);
}

void ArcService::UpdateDeviceIPConfig(const ShillClient::Device& shill_device) {
  auto shill_device_it =
      shill_devices_.find(shill_device.shill_device_interface_property);
  if (shill_device_it == shill_devices_.end()) {
    LOG(WARNING) << "Unknown shill Device " << shill_device;
    return;
  }
  shill_device_it->second = shill_device;
}

std::optional<net_base::IPv4Address> ArcService::GetArc0IPv4Address() const {
  if (!arc0_config_) {
    return std::nullopt;
  }
  return arc0_config_->arc_ipv4_address().address();
}

std::vector<std::string> ArcService::GetTapDevices() const {
  std::vector<std::string> taps;
  for (auto* c : all_configs_) {
    if (!c->tap_ifname().empty()) {
      taps.emplace_back(c->tap_ifname());
    }
  }
  return taps;
}

std::vector<const ArcService::ArcDevice*> ArcService::GetDevices() const {
  std::vector<const ArcDevice*> devices;
  for (const auto& [_, dev] : devices_) {
    devices.push_back(&dev);
  }
  return devices;
}

// static
std::string ArcService::ArcVethHostName(const ShillClient::Device& device) {
  return PrefixIfname("veth", device.shill_device_interface_property);
}

// static
std::string ArcService::ArcBridgeName(const ShillClient::Device& device) {
  return PrefixIfname("arc_", device.shill_device_interface_property);
}

std::ostream& operator<<(std::ostream& stream,
                         const ArcService::ArcDevice& arc_device) {
  stream << "{ type: " << arc_device.type()
         << ", arc_device_ifname: " << arc_device.arc_device_ifname()
         << ", arc_ipv4_addr: " << arc_device.arc_ipv4_address()
         << ", arc_device_mac_addr: "
         << MacAddressToString(arc_device.arc_device_mac_address())
         << ", bridge_ifname: " << arc_device.bridge_ifname()
         << ", bridge_ipv4_addr: " << arc_device.bridge_ipv4_address()
         << ", guest_device_ifname: " << arc_device.guest_device_ifname();
  if (arc_device.shill_device_ifname()) {
    stream << ", shill_ifname: " << *arc_device.shill_device_ifname();
  }
  return stream << '}';
}

std::ostream& operator<<(std::ostream& stream, ArcService::ArcType arc_type) {
  switch (arc_type) {
    case ArcService::ArcType::kContainer:
      return stream << "ARC Container";
    case ArcService::ArcType::kVM:
      return stream << "ARCVM";
  }
}

void ArcService::StartArcDeviceDatapath(
    const ArcService::ArcDevice& arc_device) {
  // Only create the host virtual interface and guest virtual interface for the
  // container. The TAP devices are currently always created statically ahead of
  // time.
  if (arc_type_ == ArcType::kContainer) {
    pid_t pid = static_cast<int>(id_);
    if (pid < 0) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Invalid ARC container pid " << pid;
      return;
    }
    // ARC requires multicast capability at all times. This is tested as part of
    // CTS and CDD.
    if (!datapath_->ConnectVethPair(
            pid, kArcNetnsName, arc_device.arc_device_ifname(),
            arc_device.guest_device_ifname(),
            arc_device.arc_device_mac_address(), arc_device.arc_ipv4_address(),
            /*remote_ipv6_cidr=*/std::nullopt,
            /*remote_multicast_flag=*/true)) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Cannot create virtual ethernet pair";
      return;
    }
    // Allow netd to write to /sys/class/net/arc0/mtu (b/175571457).
    if (!SetSysfsOwnerToAndroidRoot(
            pid, "net/" + arc_device.guest_device_ifname() + "/mtu")) {
      RecordEvent(metrics_, ArcServiceUmaEvent::kSetVethMtuError);
    }
  }

  // Create the associated bridge and link the host virtual device to the
  // bridge.
  if (!datapath_->AddBridge(arc_device.bridge_ifname(),
                            arc_device.bridge_ipv4_address())) {
    LOG(ERROR) << __func__ << "(" << arc_device << "): Failed to setup bridge";
    return;
  }

  if (!datapath_->AddToBridge(arc_device.bridge_ifname(),
                              arc_device.arc_device_ifname())) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to link bridge and ARC virtual interface";
    return;
  }

  if (!arc_device.shill_device_ifname()) {
    return;
  }

  // Only setup additional iptables rules for ARC Devices bound to a shill
  // Device. The iptables rules for arc0 are configured only when a VPN
  // connection exists and are triggered directly from Manager when the default
  // logical network switches to a VPN.
  const auto shill_device_it =
      shill_devices_.find(*arc_device.shill_device_ifname());
  if (shill_device_it == shill_devices_.end()) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to find shill Device";
    return;
  }

  datapath_->StartRoutingDevice(
      shill_device_it->second, arc_device.bridge_ifname(), TrafficSource::kArc);
  datapath_->AddInboundIPv4DNAT(AutoDNATTarget::kArc, shill_device_it->second,
                                arc_device.arc_ipv4_address().address());
  if (IsAdbAllowed(shill_device_it->second.type) &&
      !datapath_->AddAdbPortAccessRule(shill_device_it->second.ifname)) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to add ADB port access rule";
  }
}

void ArcService::StopArcDeviceDatapath(
    const ArcService::ArcDevice& arc_device) {
  if (arc_device.shill_device_ifname()) {
    const auto shill_device_it =
        shill_devices_.find(*arc_device.shill_device_ifname());
    if (shill_device_it == shill_devices_.end()) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Failed to find shill Device";
    } else {
      if (IsAdbAllowed(shill_device_it->second.type)) {
        datapath_->DeleteAdbPortAccessRule(shill_device_it->second.ifname);
      }
      datapath_->RemoveInboundIPv4DNAT(AutoDNATTarget::kArc,
                                       shill_device_it->second,
                                       arc_device.arc_ipv4_address().address());
      datapath_->StopRoutingDevice(arc_device.bridge_ifname());
    }
  }
  datapath_->RemoveBridge(arc_device.bridge_ifname());

  // Only destroy the host virtual interface for the container. ARCVM TAP
  // devices are removed separately when ARC stops.
  if (arc_type_ == ArcType::kContainer) {
    datapath_->RemoveInterface(arc_device.arc_device_ifname());
  }
}

}  // namespace patchpanel
