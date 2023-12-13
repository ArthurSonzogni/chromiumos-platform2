// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/rand_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/process_manager.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/iptables.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/system.h"

namespace patchpanel {

namespace {
constexpr char kTaygaBinaryPath[] = "/usr/sbin/tayga";
constexpr char kTaygaConfigFilePath[] = "/run/tayga/tayga.conf";
// Proposd in RFC 6052.
const net_base::IPv6CIDR kWellKnownNAT64Prefix =
    *net_base::IPv6CIDR::CreateFromStringAndPrefix("64:ff9b::", 96);
// Proposed in RFC 7335. This address is assigned to the tun device and used by
// IPv4-only applications to communicate with external IPv4 hosts.
const net_base::IPv4CIDR kTunnelDeviceIPv4CIDR =
    *net_base::IPv4CIDR::CreateFromStringAndPrefix("192.0.0.1", 29);
// Proposed in RFC 7335. This address is assigned to the TAYGA process and used
// for emitting ICMPv4 errors back to the host.
const net_base::IPv4CIDR kTaygaIPv4CIDR =
    *net_base::IPv4CIDR::CreateFromStringAndPrefix("192.0.0.2", 29);

constexpr char kTunnelDeviceIfName[] = "tun_nat64";
constexpr char kTaygaConfigTemplate[] = R"(tun-device $1
ipv4-addr $2
prefix $3
map $4 $5
)";

// ID for the routing table used for CLAT default routes. This is a contracted
// value with shill.
// c.f. shill/network/network_applier.cc
constexpr int kClatRoutingTableId = 249;

bool RemoveConfigFileIfExists(const base::FilePath& conf_file_path) {
  if (!base::PathExists(conf_file_path)) {
    return true;
  }
  if (!brillo::DeletePathRecursively(conf_file_path)) {
    PLOG(ERROR) << "Failed to delete file " << conf_file_path;
    return false;
  }
  return true;
}

bool NeedsClat(const ShillClient::Device& device) {
  return device.IsIPv6Only() && device.type != ShillClient::Device::Type::kVPN;
}

}  // namespace

ClatService::ClatService(Datapath* datapath,
                         net_base::ProcessManager* process_manager,
                         System* system)
    : datapath_(datapath), process_manager_(process_manager), system_(system) {
  DCHECK(datapath);
  DCHECK(process_manager);
  DCHECK(system);
}

ClatService::~ClatService() {
  StopClat();
}

void ClatService::Enable() {
  if (is_enabled_) {
    return;
  }

  is_enabled_ = true;
  // Starts CLAT immediately, if the default network is CLAT-eligible when it
  // gets enabled.
  if (clat_running_device_) {
    StartClat(clat_running_device_.value());
  }
}

void ClatService::Disable() {
  if (!is_enabled_) {
    return;
  }
  // We keep `clat_running_device_` here because we want to start CLAT
  // immediately after the feature is enabled again.
  StopClat(/*clear_running_device=*/false);

  is_enabled_ = false;
}

void ClatService::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  bool need_stop =
      clat_running_device_ && !(new_device && IsClatRunningDevice(*new_device));

  if (need_stop) {
    StopClat();
  }

  // CLAT should be started when CLAT is not running and the new default logical
  // device is IPv6-only.
  bool need_start =
      new_device && !clat_running_device_ && NeedsClat(*new_device);

  if (need_start) {
    StartClat(*new_device);
  }

  return;
}

// TODO(b/278970851): Add delay between the occurrence of this event and the
// execution of StartClat().
// https://chromium-review.googlesource.com/c/chromiumos/platform2/+/4803285/comment/ff1aa754_26e63d28/
void ClatService::OnDefaultLogicalDeviceIPConfigChanged(
    const ShillClient::Device& default_logical_device) {
  if (!clat_running_device_) {
    if (NeedsClat(default_logical_device)) {
      StartClat(default_logical_device);
    }
    return;
  }

  // It is unexpected that CLAT is running on the device other than the default
  // logical device.
  if (!IsClatRunningDevice(default_logical_device)) {
    LOG(ERROR) << "CLAT is running on the device " << clat_running_device_
               << " although the default logical device is "
               << default_logical_device.ifname;
    StopClat();
    return;
  }

  // CLAT is running on the default logical device.

  DCHECK(!clat_running_device_->ipconfig.ipv4_cidr.has_value());
  DCHECK(clat_running_device_->ipconfig.ipv6_cidr.has_value());

  if (!NeedsClat(default_logical_device)) {
    StopClat();
    return;
  }

  if (clat_running_device_->ipconfig.ipv6_cidr !=
      default_logical_device.ipconfig.ipv6_cidr) {
    // TODO(b/278970851): Optimize the restart process of CLAT. Resources
    // such as the tun device can be reused.
    StopClat();

    StartClat(default_logical_device);
  }
}

void ClatService::StartClat(const ShillClient::Device& shill_device) {
  // Even if CLAT is disabled, we keep track of the device on which CLAT
  // should be running so that we can start CLAT immediately after it's
  // enabled.
  clat_running_device_ = shill_device;

  if (!is_enabled_) {
    return;
  }

  if (!shill_device.ipconfig.ipv6_cidr) {
    LOG(ERROR) << shill_device << " doesn't have an IPv6 address";
    return;
  }

  auto clat_ipv6_cidr = AddressManager::GetRandomizedIPv6Address(
      shill_device.ipconfig.ipv6_cidr->GetPrefixCIDR());
  if (!clat_ipv6_cidr) {
    LOG(ERROR) << "Failed to get randomized IPv6 address from " << shill_device;
    return;
  }
  clat_ipv6_addr_ = clat_ipv6_cidr->address();

  if (!CreateConfigFile(kTunnelDeviceIfName, clat_ipv6_addr_.value())) {
    LOG(ERROR) << "Failed to create " << kTaygaConfigFilePath;
    StopClat();
    return;
  }

  if (datapath_->AddTunTap(kTunnelDeviceIfName, std::nullopt,
                           kTunnelDeviceIPv4CIDR, "",
                           DeviceMode::kTun) != kTunnelDeviceIfName) {
    LOG(ERROR) << "Failed to create a tun device for CLAT";
    StopClat();
    return;
  }

  if (!StartTayga()) {
    LOG(ERROR) << "Failed to start TAYGA on " << shill_device;
    StopClat();
    return;
  }

  if (!datapath_->ModifyClatAcceptRules(Iptables::Command::kA,
                                        kTunnelDeviceIfName)) {
    LOG(ERROR) << "Failed to add rules for CLAT in ip6tables";
    StopClat();
    return;
  }

  // The prefix length has to be /128 to add a route for only one IPv6 address.
  if (!datapath_->AddIPv6HostRoute(
          kTunnelDeviceIfName, *net_base::IPv6CIDR::CreateFromAddressAndPrefix(
                                   clat_ipv6_addr_.value(), 128))) {
    LOG(ERROR) << "Failed to add a route to " << kTunnelDeviceIfName;
    StopClat();
    return;
  }

  if (!datapath_->AddIPv6NeighborProxy(clat_running_device_->ifname,
                                       clat_ipv6_addr_.value())) {
    LOG(ERROR) << "Failed to add a ND proxy with interface "
               << kTunnelDeviceIfName << " and IPv6 address "
               << clat_ipv6_addr_.value();
    StopClat();
    return;
  }

  if (!datapath_->AddIPv4RouteToTable(kTunnelDeviceIfName, net_base::IPv4CIDR(),
                                      kClatRoutingTableId)) {
    LOG(ERROR) << "Failed to add a default route to table "
               << kClatRoutingTableId;
    StopClat();
    return;
  }

  LOG(INFO) << "CLAT has started on the device " << clat_running_device_.value()
            << " and with the IPv6 address " << clat_ipv6_addr_->ToString();
}

void ClatService::StopClat(bool clear_running_device) {
  if (!is_enabled_) {
    if (clear_running_device) {
      clat_running_device_.reset();
    }
    clat_ipv6_addr_.reset();
    return;
  }

  if (!(clat_running_device_ && clat_ipv6_addr_)) {
    LOG(INFO) << "No need to clean up CLAT configurations";
    return;
  }

  datapath_->DeleteIPv4RouteFromTable(kTunnelDeviceIfName, net_base::IPv4CIDR(),
                                      kClatRoutingTableId);

  datapath_->RemoveIPv6NeighborProxy(clat_running_device_->ifname,
                                     clat_ipv6_addr_.value());

  datapath_->ModifyClatAcceptRules(Iptables::Command::kD, kTunnelDeviceIfName);

  StopTayga();

  // The prefix length has to be /128 to remove a route for only one IPv6
  // address.
  datapath_->RemoveIPv6HostRoute(
      *net_base::IPv6CIDR::CreateFromAddressAndPrefix(clat_ipv6_addr_.value(),
                                                      128));

  datapath_->RemoveTunTap(kTunnelDeviceIfName, DeviceMode::kTun);

  RemoveConfigFileIfExists(base::FilePath(kTaygaConfigFilePath));

  LOG(INFO) << "CLAT has stopped on the device " << clat_running_device_.value()
            << " and with the IPv6 address " << clat_ipv6_addr_->ToString();

  if (clear_running_device) {
    clat_running_device_.reset();
  }
  clat_ipv6_addr_.reset();
}

void ClatService::SetClatRunningDeviceForTest(
    const ShillClient::Device& shill_device) {
  clat_running_device_ = shill_device;
}

void ClatService::ResetClatRunningDeviceForTest() {
  clat_running_device_.reset();
}

bool ClatService::IsClatRunningDevice(const ShillClient::Device& shill_device) {
  if (!clat_running_device_) {
    return false;
  }

  return shill_device.ifname == clat_running_device_->ifname;
}

bool ClatService::CreateConfigFile(
    const std::string& ifname, const net_base::IPv6Address& clat_ipv6_addr) {
  const std::string contents = base::ReplaceStringPlaceholders(
      kTaygaConfigTemplate,
      {
          /*$1=*/std::string(kTunnelDeviceIfName),
          /*$2=*/kTaygaIPv4CIDR.address().ToString(),
          /*$3=*/kWellKnownNAT64Prefix.ToString(),
          /*$4=*/kTunnelDeviceIPv4CIDR.address().ToString(),
          /*$5=*/clat_ipv6_addr.ToString(),
      },
      nullptr);

  return system_->WriteConfigFile(base::FilePath(kTaygaConfigFilePath),
                                  contents);
}

bool ClatService::StartTayga() {
  std::vector<std::string> args = {
      "-n",
      "-c",
      kTaygaConfigFilePath,
  };

  net_base::ProcessManager::MinijailOptions minijail_options;
  minijail_options.user = kPatchpaneldUser;
  minijail_options.group = kPatchpaneldGroup;
  minijail_options.capmask = CAP_TO_MASK(CAP_NET_RAW);
  // This gives TAYGA permissions of grou tun, which is necessary for it to open
  // /dev/net/tun and configure the tun device.
  minijail_options.inherit_supplementary_groups = true;

  tayga_pid_ = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kTaygaBinaryPath), args, {}, minijail_options,
      base::DoNothing());

  return tayga_pid_ >= 0;
}

void ClatService::StopTayga() {
  if (tayga_pid_ == -1) {
    return;
  }

  if (!brillo::Process::ProcessExists(tayga_pid_)) {
    LOG(WARNING) << "TAYGA[" << tayga_pid_ << "] already stopped";
    tayga_pid_ = -1;
    return;
  }

  process_manager_->StopProcessAndBlock(tayga_pid_);
  tayga_pid_ = -1;
}

}  // namespace patchpanel
