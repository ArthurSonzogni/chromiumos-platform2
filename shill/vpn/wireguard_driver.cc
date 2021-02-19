// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/process_manager.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const WireguardDriver*) {
  return "(wireguard_driver)";
}
}  // namespace Logging

namespace {

const char kWireguardPath[] = "/usr/sbin/wireguard";
const char kWireguardToolsPath[] = "/usr/sbin/wg";
const char kDefaultInterfaceName[] = "wg0";

// Directory where wireguard configuration files are exported. The owner of this
// directory is vpn:vpn, so both shill and wireguard client can access it.
const char kWireguardConfigDir[] = "/run/wireguard";

// Timeout value for spawning the userspace wireguard process and configuring
// the interface via wireguard-tools.
constexpr base::TimeDelta kConnectTimeout = base::TimeDelta::FromSeconds(10);

// User and group we use to run wireguard binaries.
const char kVpnUser[] = "vpn";
const char kVpnGroup[] = "vpn";
constexpr gid_t kVpnGid = 20174;

}  // namespace

// TODO(b/177876632): These should be moved to dbus-constants once we have
// confirmed the fields we need.
const char kWireguardPrivateKey[] = "Wireguard.PrivateKey";
const char kWireguardAddress[] = "Wireguard.Address";
const char kWireguardPeerPublicKey[] = "Wireguard.Peer.PublicKey";
const char kWireguardPeerPresharedKey[] = "Wireguard.Peer.PresharedKey";
const char kWireguardPeerEndPoint[] = "Wireguard.Peer.EndPoint";
const char kWireguardPeerAllowedIPs[] = "Wireguard.Peer.AllowedIPs";
const char kWireguardPeerPersistentKeepalive[] =
    "Wireguard.Peer.PersistentKeepalive";

// static
const VPNDriver::Property WireguardDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},

    // Properties for the interface. ListenPort is not here since we current
    // only support the "client mode".
    {kWireguardPrivateKey, 0},
    // Address for the wireguard interface. Note that this is not required for
    // configuring the interface by wireguard-tools, but is required for
    // populating routing entries in IPProperties, so we put it here instead of
    // in the StaticIPParameters. See PopulateIPProperties() for details.
    // TODO(b/177876632): Verify that putting other properties for the interface
    // (i.e., DNS and MTU) are in the StaticIPParameters works.
    {kWireguardAddress, 0},

    // Properties for a peer. Currently we only support one peer.
    {kWireguardPeerPublicKey, 0},
    {kWireguardPeerPresharedKey, 0},
    {kWireguardPeerEndPoint, 0},
    // Note that AllowedIPs is a list of CIDR addresses. We treat it as a
    // comma-separated string instead of an array here for simplicity now.
    {kWireguardPeerAllowedIPs, 0},
    {kWireguardPeerPersistentKeepalive, 0},
};

WireguardDriver::WireguardDriver(Manager* manager,
                                 ProcessManager* process_manager)
    : VPNDriver(
          manager, process_manager, kProperties, base::size(kProperties)) {}

WireguardDriver::~WireguardDriver() {
  Cleanup();
}

base::TimeDelta WireguardDriver::ConnectAsync(EventHandler* event_handler) {
  SLOG(this, 2) << __func__;
  event_handler_ = event_handler;
  dispatcher()->PostTask(FROM_HERE,
                         base::BindRepeating(&WireguardDriver::ConnectInternal,
                                             weak_factory_.GetWeakPtr()));
  return kConnectTimeout;
}

void WireguardDriver::Disconnect() {
  SLOG(this, 2) << __func__;
  Cleanup();
  event_handler_ = nullptr;
}

IPConfig::Properties WireguardDriver::GetIPProperties() const {
  return ip_properties_;
}

std::string WireguardDriver::GetProviderType() const {
  return kProviderWireguard;
}

void WireguardDriver::OnConnectTimeout() {
  FailService(Service::kFailureConnect, "Connect timeout");
}

void WireguardDriver::ConnectInternal() {
  // Claims the interface before the wireguard process creates it.
  // TODO(b/177876632): Actually when the tunnel interface is ready, it cannot
  // guarantee that the wireguard-tools can talk with the userspace wireguard
  // process now. We should also wait for another event that the UAPI socket
  // appears (which is a UNIX-domain socket created by the userspace wireguard
  // process at a fixed path: `/var/run/wireguard/wg0.sock`).
  manager()->device_info()->AddVirtualInterfaceReadyCallback(
      kDefaultInterfaceName,
      base::BindOnce(&WireguardDriver::ConfigureInterface,
                     weak_factory_.GetWeakPtr()));

  if (!SpawnWireguard()) {
    FailService(Service::kFailureInternal, "Failed to spawn wireguard process");
  }
}

bool WireguardDriver::SpawnWireguard() {
  SLOG(this, 2) << __func__;

  // TODO(b/177876632): Change this part after we decide the userspace binary to
  // use. For wireguard-go, we need to change the way to invoke minijail; for
  // wireugard-rs, we need to add `--disable-drop-privileges` or change the
  // capmask.
  std::vector<std::string> args = {
      "--foreground",
      kDefaultInterfaceName,
  };
  uint64_t capmask = CAP_TO_MASK(CAP_NET_ADMIN);
  wireguard_pid_ = process_manager()->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kWireguardPath), args,
      /*environment=*/{}, kVpnUser, kVpnGroup, capmask,
      /*inherit_supplementary_groups=*/true, /*close_nonstd_fds=*/true,
      base::BindRepeating(&WireguardDriver::WireguardProcessExited,
                          weak_factory_.GetWeakPtr()));
  return wireguard_pid_ > -1;
}

void WireguardDriver::WireguardProcessExited(int exit_code) {
  wireguard_pid_ = -1;
  FailService(
      Service::kFailureInternal,
      base::StringPrintf("wireguard process exited unexpectedly with code=%d",
                         exit_code));
}

bool WireguardDriver::AppendConfig(const std::string& key_in_config,
                                   const std::string& key_in_args,
                                   bool is_required,
                                   std::vector<std::string>* lines) {
  std::string value = args()->Lookup<std::string>(key_in_args, "");
  if (value.empty()) {
    if (is_required) {
      LOG(ERROR) << key_in_args << " is required but is empty or not set.";
      return false;
    }
    return true;
  }
  lines->push_back(base::StrCat({key_in_config, "=", value}));
  return true;
}

bool WireguardDriver::GenerateConfigFile() {
  std::vector<std::string> lines;

  // [Interface] section
  lines.push_back("[Interface]");
  if (!AppendConfig("PrivateKey", kWireguardPrivateKey, true, &lines)) {
    return false;
  }
  // TODO(b/177876632): FwMark can be set here.

  lines.push_back("");

  // [Peer] section
  lines.push_back("[Peer]");
  if (!AppendConfig("PublicKey", kWireguardPeerPublicKey, true, &lines) ||
      !AppendConfig("PresharedKey", kWireguardPeerPresharedKey, false,
                    &lines) ||
      !AppendConfig("AllowedIPs", kWireguardPeerAllowedIPs, true, &lines) ||
      !AppendConfig("EndPoint", kWireguardPeerEndPoint, true, &lines) ||
      !AppendConfig("PersistentKeepalive", kWireguardPeerPersistentKeepalive,
                    false, &lines)) {
    return false;
  }

  // Writes |lines| into the file.
  const base::FilePath config_dir = base::FilePath(kWireguardConfigDir);
  if (!base::CreateTemporaryFileInDir(config_dir, &config_file_)) {
    LOG(ERROR) << "Failed to create wireguard config file.";
    return false;
  }

  std::string contents = base::JoinString(lines, "\n");
  contents.append("\n");
  if (!base::WriteFile(config_file_, contents)) {
    LOG(ERROR) << "Failed to write wireguard config file";
    return false;
  }

  // Makes the config file group-readable and change its group to "vpn". Note
  // that the owner of a file may change the group of the file to any group of
  // which that owner is a member, so we can change the group to "vpn" here
  // since "shill" is a member of "vpn".
  if (chmod(config_file_.value().c_str(), S_IRGRP) != 0) {
    PLOG(ERROR) << "Failed to make config file group-readable";
    return false;
  }
  if (chown(config_file_.value().c_str(), -1, kVpnGid) != 0) {
    PLOG(ERROR) << "Failed to change gid of config file";
    return false;
  }

  return true;
}

void WireguardDriver::ConfigureInterface(const std::string& /*interface_name*/,
                                         int interface_index) {
  SLOG(this, 2) << __func__;

  if (!event_handler_) {
    LOG(ERROR) << "Missing event_handler_";
    Cleanup();
    return;
  }

  interface_index_ = interface_index;

  if (!GenerateConfigFile()) {
    FailService(Service::kFailureInternal, "Failed to generate config file");
    return;
  }

  std::vector<std::string> args = {"setconf", kDefaultInterfaceName,
                                   config_file_.value()};
  pid_t pid = process_manager()->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kWireguardToolsPath), args,
      /*environment=*/{}, kVpnUser, kVpnGroup, /*caps=*/0, true, true,
      base::BindRepeating(&WireguardDriver::OnConfigurationDone,
                          weak_factory_.GetWeakPtr()));
  if (pid == -1) {
    FailService(Service::kFailureInternal, "Failed to run `wg setconf`");
    return;
  }
}

void WireguardDriver::OnConfigurationDone(int exit_code) {
  SLOG(this, 2) << __func__ << ": exit_code=" << exit_code;

  if (exit_code != 0) {
    FailService(
        Service::kFailureInternal,
        base::StringPrintf("Failed to run `wg setconf`, code=%d", exit_code));
    return;
  }

  if (!PopulateIPProperties()) {
    FailService(Service::kFailureInternal, "Failed to populate ip properties");
    return;
  }

  event_handler_->OnDriverConnected(kDefaultInterfaceName, interface_index_);
}

bool WireguardDriver::PopulateIPProperties() {
  ip_properties_.default_route = false;

  const auto address =
      IPAddress(args()->Lookup<std::string>(kWireguardAddress, ""));
  if (!address.IsValid()) {
    LOG(ERROR) << "WireguardAddress property is not valid";
    return false;
  }
  ip_properties_.address_family = address.family();
  ip_properties_.address = address.ToString();

  // When we arrive here, the value of AllowedIPs has already been validated
  // by wireguard-tools. AllowedIPs is comma-separated list of CIDR-notation
  // addresses (e.g., "10.8.0.1/16,192.168.1.1/24").
  std::string allowed_ips_str =
      args()->Lookup<std::string>(kWireguardPeerAllowedIPs, "");
  std::vector<std::string> allowed_ip_list = base::SplitString(
      allowed_ips_str, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& allowed_ip_str : allowed_ip_list) {
    IPAddress allowed_ip;
    // Currently only supports IPv4 addresses.
    allowed_ip.set_family(IPAddress::kFamilyIPv4);
    if (!allowed_ip.SetAddressAndPrefixFromString(allowed_ip_str)) {
      LOG(DFATAL) << "Invalid allowed ip: " << allowed_ip_str;
      return false;
    }
    // We don't need a gateway here, so use the "default" address as the
    // gateways, and then RoutingTable will skip RTA_GATEWAY when installing
    // this entry.
    ip_properties_.routes.push_back({allowed_ip.GetNetworkPart().ToString(),
                                     static_cast<int>(allowed_ip.prefix()),
                                     /*gateway=*/"0.0.0.0"});
  }
  return true;
}

void WireguardDriver::FailService(Service::ConnectFailure failure,
                                  const std::string& error_details) {
  LOG(ERROR) << "Driver error: " << error_details;
  Cleanup();
  if (event_handler_) {
    event_handler_->OnDriverFailure(failure, error_details);
    event_handler_ = nullptr;
  }
}

void WireguardDriver::Cleanup() {
  if (wireguard_pid_ != -1) {
    process_manager()->StopProcess(wireguard_pid_);
    wireguard_pid_ = -1;
  }
  interface_index_ = -1;
  ip_properties_ = {};
  if (!config_file_.empty()) {
    if (!base::DeleteFile(config_file_)) {
      LOG(ERROR) << "Failed to delete wireguard config file";
    }
    config_file_.clear();
  }
}

}  // namespace shill
