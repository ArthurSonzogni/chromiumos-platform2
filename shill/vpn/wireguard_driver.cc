// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
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
const char kDefaultInterfaceName[] = "wg0";

// Timeout value for spawning the userspace wireguard process and configuring
// the interface via wireguard-tools.
constexpr base::TimeDelta kConnectTimeout = base::TimeDelta::FromSeconds(10);

// User and group we use to run wireguard binaries.
const char kVpnUser[] = "vpn";
const char kVpnGroup[] = "vpn";

}  // namespace

// static
const VPNDriver::Property WireguardDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
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

// TODO(b/177876632): have the real implementation
IPConfig::Properties WireguardDriver::GetIPProperties() const {
  return {};
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

void WireguardDriver::ConfigureInterface(const std::string& /*interface_name*/,
                                         int /*interface_index*/) {
  SLOG(this, 2) << __func__;
  // TODO(b/177876632): use wireguard-tools to configure the interface.
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
}

}  // namespace shill
