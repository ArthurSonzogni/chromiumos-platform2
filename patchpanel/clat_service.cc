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
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/process_manager.h>
#include <chromeos/net-base/technology.h>

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
    PLOG(ERROR) << __func__ << ": Failed to delete file " << conf_file_path;
    return false;
  }
  return true;
}

bool NeedsClat(const ShillClient::Device& device) {
  return device.IsIPv6Only() &&
         device.technology != net_base::Technology::kVPN &&
         device.technology != net_base::Technology::kWiFiDirect;
}

net_base::IPv6CIDR GetNAT64Prefix(const ShillClient::Device& shill_device) {
  return shill_device.network_config.pref64.value_or(kWellKnownNAT64Prefix);
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
  bool was_running = IsClatRunning();
  // CLAT should run if the new default logical device is IPv6-only.
  bool should_run = new_device && NeedsClat(*new_device);
  // CLAT should restart if the configuration has changed: the logical default
  // shill Device has changed or the NAT64 prefix has changed.
  bool has_config_changed =
      new_device && (HasClatRunningDeviceChanged(*new_device) ||
                     HasNAT64PrefixChanged(*new_device));
  if (!was_running && should_run) {
    StartClat(*new_device);
  } else if (was_running && !should_run) {
    StopClat();
  } else if (was_running && should_run && has_config_changed) {
    StopClat();
    StartClat(*new_device);
  }
}

// TODO(b/278970851): Add delay between the occurrence of this event and the
// execution of StartClat().
// https://chromium-review.googlesource.com/c/chromiumos/platform2/+/4803285/comment/ff1aa754_26e63d28/
void ClatService::OnDefaultLogicalDeviceIPConfigChanged(
    const ShillClient::Device& default_logical_device) {
  if (!IsClatRunning()) {
    if (NeedsClat(default_logical_device)) {
      StartClat(default_logical_device);
    }
    return;
  }

  // It is unexpected that CLAT is running on the device other than the default
  // logical device.
  if (HasClatRunningDeviceChanged(default_logical_device)) {
    LOG(ERROR) << __func__ << ": CLAT is running on the device "
               << clat_running_device_
               << " although the default logical device is "
               << default_logical_device.ifname;
    StopClat();
    return;
  }

  // CLAT is running on the default logical device.

  DCHECK(!clat_running_device_->network_config.ipv4_address.has_value());
  DCHECK(!clat_running_device_->network_config.ipv6_addresses.empty());

  if (!NeedsClat(default_logical_device)) {
    StopClat();
    return;
  }

  if (clat_running_device_->network_config.ipv6_addresses[0] !=
          default_logical_device.network_config.ipv6_addresses[0] ||
      HasNAT64PrefixChanged(default_logical_device)) {
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

  if (shill_device.network_config.ipv6_addresses.empty()) {
    LOG(ERROR) << __func__ << ": No IPv6 address on " << shill_device;
    return;
  }

  auto current_subnet =
      shill_device.network_config.ipv6_delegated_prefixes.empty()
          ? shill_device.network_config.ipv6_addresses[0].GetPrefixCIDR()
          : shill_device.network_config.ipv6_delegated_prefixes[0];
  auto clat_ipv6_cidr =
      AddressManager::GetRandomizedIPv6Address(current_subnet);
  if (!clat_ipv6_cidr) {
    LOG(ERROR) << __func__ << ": Failed to get randomized IPv6 address from "
               << shill_device;
    return;
  }
  clat_ipv6_addr_ = clat_ipv6_cidr->address();

  if (!CreateConfigFile(GetNAT64Prefix(shill_device),
                        clat_ipv6_addr_.value())) {
    LOG(ERROR) << __func__ << ": Failed to create " << kTaygaConfigFilePath;
    StopClat();
    return;
  }

  if (datapath_->AddTunTap(kTunnelDeviceIfName, std::nullopt,
                           kTunnelDeviceIPv4CIDR, "",
                           DeviceMode::kTun) != kTunnelDeviceIfName) {
    LOG(ERROR) << __func__ << ": Failed to create a tun device for CLAT";
    StopClat();
    return;
  }

  if (!StartTayga()) {
    LOG(ERROR) << __func__ << ": Failed to start TAYGA on " << shill_device;
    StopClat();
    return;
  }

  if (!datapath_->ModifyClatAcceptRules(Iptables::Command::kA,
                                        kTunnelDeviceIfName)) {
    LOG(ERROR) << __func__ << ": Failed to add rules for CLAT in ip6tables";
    StopClat();
    return;
  }

  // The prefix length has to be /128 to add a route for only one IPv6 address.
  if (!datapath_->AddIPv6HostRoute(
          kTunnelDeviceIfName, *net_base::IPv6CIDR::CreateFromAddressAndPrefix(
                                   clat_ipv6_addr_.value(), 128))) {
    LOG(ERROR) << __func__ << ": Failed to add a route to "
               << kTunnelDeviceIfName;
    StopClat();
    return;
  }

  if (!datapath_->AddIPv6NeighborProxy(clat_running_device_->ifname,
                                       clat_ipv6_addr_.value())) {
    LOG(ERROR) << __func__ << ": Failed to add a ND proxy with interface "
               << kTunnelDeviceIfName << " and IPv6 address "
               << clat_ipv6_addr_.value();
    StopClat();
    return;
  }

  if (!datapath_->AddIPv4RouteToTable(kTunnelDeviceIfName, net_base::IPv4CIDR(),
                                      kClatRoutingTableId)) {
    LOG(ERROR) << __func__ << ": Failed to add a default route to table "
               << kClatRoutingTableId;
    StopClat();
    return;
  }

  LOG(INFO) << __func__ << ": address: " << *clat_ipv6_addr_
            << ", prefix: " << GetNAT64Prefix(*clat_running_device_)
            << ", device: " << clat_running_device_.value();
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
    LOG(INFO) << __func__ << ": No need to clean up CLAT configurations";
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

  LOG(INFO) << __func__ << ": address: " << *clat_ipv6_addr_
            << ", prefix: " << GetNAT64Prefix(*clat_running_device_)
            << ", device: " << clat_running_device_.value();

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

bool ClatService::HasClatRunningDeviceChanged(
    const ShillClient::Device& shill_device) {
  if (!clat_running_device_) {
    return true;
  }

  return shill_device.ifname != clat_running_device_->ifname;
}

bool ClatService::HasNAT64PrefixChanged(
    const ShillClient::Device& shill_device) {
  if (!clat_running_device_) {
    return true;
  }

  return GetNAT64Prefix(shill_device) != GetNAT64Prefix(*clat_running_device_);
}

bool ClatService::IsClatRunning() const {
  return clat_running_device_.has_value();
}

bool ClatService::CreateConfigFile(
    const net_base::IPv6CIDR& nat64_prefix,
    const net_base::IPv6Address& clat_ipv6_addr) {
  const std::string contents = base::ReplaceStringPlaceholders(
      kTaygaConfigTemplate,
      {
          /*$1=*/std::string(kTunnelDeviceIfName),
          /*$2=*/kTaygaIPv4CIDR.address().ToString(),
          /*$3=*/nat64_prefix.ToString(),
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
    LOG(WARNING) << __func__ << ": TAYGA[" << tayga_pid_ << "] already stopped";
    tayga_pid_ = -1;
    return;
  }

  process_manager_->StopProcessAndBlock(tayga_pid_);
  tayga_pid_ = -1;
}

}  // namespace patchpanel
