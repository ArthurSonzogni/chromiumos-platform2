// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <optional>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {
constexpr char kTaygaConfigFilePath[] = "/run/tayga/tayga.conf";
// Proposd in RFC 6052.
const net_base::IPv6CIDR kWellKnownNAT64Prefix =
    *net_base::IPv6CIDR::CreateFromStringAndPrefix("64:ff9b::", 96);

// Proposed in RFC 7335. This address is assigned to the tun device and used by
// IPv4-only applications to communicate with external IPv4 hosts.
constexpr net_base::IPv4Address kTunnelDeviceIPv4Address(192, 0, 0, 1);
// Proposed in RFC 7335. This address is assigned to the TAYGA process and used
// for emitting ICMPv4 errors back to the host.
constexpr net_base::IPv4Address kTaygaIPv4Address(192, 0, 0, 2);

constexpr char kTunnelDeviceIfName[] = "tun_nat64";

constexpr char kTaygaConfigTemplate[] = R"(tun-device $1
ipv4-addr $2
prefix $3
map $4 $5
)";

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

// Creates a config file `/run/tayga/tayga.conf`. An old config file will be
// overwritten by a new one.
bool CreateConfigFile(const std::string& ifname,
                      const net_base::IPv6Address& clat_ipv6_addr) {
  const std::string contents = base::ReplaceStringPlaceholders(
      kTaygaConfigTemplate,
      {
          /*$1=*/std::string(kTunnelDeviceIfName),
          /*$2=*/kTaygaIPv4Address.ToString(),
          /*$3=*/kWellKnownNAT64Prefix.ToString(),
          /*$4=*/kTunnelDeviceIPv4Address.ToString(),
          /*$5=*/clat_ipv6_addr.ToString(),
      },
      nullptr);

  // TODO(b/278970851): Replace this logic with System::WriteConfigFile.
  const base::FilePath& conf_file_path = base::FilePath(kTaygaConfigFilePath);
  if (!base::WriteFile(conf_file_path, contents)) {
    PLOG(ERROR) << "Failed to write config file of TAYGA";
    return false;
  }

  if (chmod(conf_file_path.value().c_str(), S_IRUSR | S_IRGRP | S_IWUSR)) {
    PLOG(ERROR) << "Failed to set permissions on " << conf_file_path;
    RemoveConfigFileIfExists(conf_file_path);

    return false;
  }

  if (chown(conf_file_path.value().c_str(), kPatchpaneldUid, kPatchpaneldGid) !=
      0) {
    PLOG(ERROR) << "Failed to change owner group of configuration file "
                << conf_file_path;
    RemoveConfigFileIfExists(conf_file_path);

    return false;
  }

  return true;
}

}  // namespace

ClatService::ClatService() = default;

// TODO(b/278970851): Do the actual implementation
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
  is_enabled_ = false;

  // We keep `clat_running_device_` here because we want to start CLAT
  // immediately after the feature is enabled again.
  StopClat(/*clear_running_device=*/false);
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
      new_device && !clat_running_device_ && new_device->IsIPv6Only();

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
    if (default_logical_device.IsIPv6Only()) {
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

  if (!default_logical_device.IsIPv6Only()) {
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
    LOG(ERROR) << shill_device << " doesn't have"
               << " an IPv6 address";
    return;
  }

  const auto ipv6_cidr = AddressManager::GetRandomizedIPv6Address(
      shill_device.ipconfig.ipv6_cidr->GetPrefixCIDR());
  if (!CreateConfigFile(kTunnelDeviceIfName, ipv6_cidr.address())) {
    LOG(ERROR) << "Failed to create " << kTaygaConfigFilePath;
    return;
  }

  // TODO(b/278970851): Do the actual implementation to set up network
  // configurations for CLAT:
  // - Create the tun device
  // - Add a route to the tun device
  // - Start TAYGA process
  // - Add ND proxy
  // - Add default route in table 249
}

void ClatService::StopClat(bool clear_running_device) {
  if (!is_enabled_) {
    if (clear_running_device) {
      clat_running_device_.reset();
    }
    return;
  }

  if (!clat_running_device_) {
    return;
  }

  // TODO(b/278970851): Do the actual implementation to clean up network
  // configurations for CLAT:
  // - Remove default route in table 249
  // - Remove ND proxy
  // - Kill TAYGA process
  // - Remove a route to the tun device
  // - Remove the tun device

  RemoveConfigFileIfExists(base::FilePath(kTaygaConfigFilePath));
  if (clear_running_device) {
    clat_running_device_.reset();
  }
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

}  // namespace patchpanel
