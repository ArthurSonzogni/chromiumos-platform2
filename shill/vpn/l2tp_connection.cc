// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/l2tp_connection.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/check.h>
#include <base/files/file_util.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/ppp_daemon.h"
#include "shill/ppp_device.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

namespace {

// TODO(b/165170125): Consider using /run/xl2tpd folder.
constexpr char kRunDir[] = "/run/l2tpipsec_vpn";
constexpr char kXl2tpdPath[] = "/usr/sbin/xl2tpd";
constexpr char kL2TPDConfigFileName[] = "l2tpd.conf";
constexpr char kL2TPDControlFileName[] = "l2tpd.control";
constexpr char kPPPDConfigFileName[] = "pppd.conf";

// Environment variable available to ppp plugin to know the resolved address
// of the L2TP server.
const char kLnsAddress[] = "LNS_ADDRESS";

}  // namespace

L2TPConnection::L2TPConnection(std::unique_ptr<Config> config,
                               std::unique_ptr<Callbacks> callbacks,
                               ControlInterface* control_interface,
                               DeviceInfo* device_info,
                               EventDispatcher* dispatcher,
                               ProcessManager* process_manager)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)),
      control_interface_(control_interface),
      device_info_(device_info),
      process_manager_(process_manager),
      vpn_util_(VPNUtil::New()) {}

L2TPConnection::~L2TPConnection() {
  if (state() == State::kIdle || state() == State::kStopped) {
    return;
  }

  // This is unexpected but cannot be fully avoided. Call OnDisconnect() to make
  // sure resources are released.
  LOG(WARNING) << "Destructor called but the current state is " << state();
  OnDisconnect();
}

void L2TPConnection::OnConnect() {
  temp_dir_ = vpn_util_->CreateScopedTempDir(base::FilePath(kRunDir));

  if (!WritePPPDConfig()) {
    NotifyFailure(Service::kFailureInternal,
                  "Failed to write pppd config file");
    return;
  }

  if (!WriteL2TPDConfig()) {
    NotifyFailure(Service::kFailureInternal,
                  "Failed to write xl2tpd config file");
    return;
  }

  StartXl2tpd();
}

void L2TPConnection::GetLogin(std::string* user, std::string* password) {
  LOG(INFO) << "Login requested.";
  if (config_->user.empty()) {
    LOG(ERROR) << "User not set.";
    return;
  }

  // TODO(b/165170125): Add support for using login password.

  if (config_->password.empty()) {
    LOG(ERROR) << "Password not set.";
    return;
  }

  *user = config_->user;
  *password = config_->password;
}

void L2TPConnection::Notify(const std::string& reason,
                            const std::map<std::string, std::string>& dict) {
  // TODO(b/165170125): On failure, check the reason (e.g., if it is an
  // authentication failure).

  if (reason == kPPPReasonAuthenticating || reason == kPPPReasonAuthenticated) {
    // These are uninteresting intermediate states that do not indicate failure.
    return;
  }

  if (reason != kPPPReasonConnect) {
    if (!IsConnectingOrConnected()) {
      // We have notified the upper layer, or the disconnect is triggered by the
      // upper layer. In both cases, we don't need call NotifyFailure().
      LOG(INFO) << "pppd notifies us of " << reason << ", the current state is "
                << state();
      return;
    }
    NotifyFailure(Service::kFailureInternal, "pppd disconnected");
    return;
  }

  // The message is kPPPReasonConnect. Checks if we are in the connecting state
  // at first.
  if (state() != State::kConnecting) {
    LOG(WARNING) << "pppd notifies us of " << reason
                 << ", the current state is " << state();
    return;
  }

  std::string interface_name = PPPDevice::GetInterfaceName(dict);
  IPConfig::Properties ip_properties = PPPDevice::ParseIPConfiguration(dict);

  // There is no IPv6 support for L2TP/IPsec VPN at this moment, so create a
  // blackhole route for IPv6 traffic after establishing a IPv4 VPN.
  ip_properties.blackhole_ipv6 = true;

  // Reduce MTU to the minimum viable for IPv6, since the IPsec layer consumes
  // some variable portion of the payload.  Although this system does not yet
  // support IPv6, it is a reasonable value to start with, since the minimum
  // IPv6 packet size will plausibly be a size any gateway would support, and
  // is also larger than the IPv4 minimum size.
  ip_properties.mtu = IPConfig::kMinIPv6MTU;

  ip_properties.method = kTypeVPN;

  // Notify() could be invoked either before or after the creation of the ppp
  // interface. We need to make sure that the interface is ready (by checking
  // DeviceInfo) before invoking the connected callback here.
  int interface_index = device_info_->GetIndex(interface_name);
  if (interface_index != -1) {
    NotifyConnected(interface_name, interface_index, ip_properties);
  } else {
    device_info_->AddVirtualInterfaceReadyCallback(
        interface_name,
        base::BindOnce(&L2TPConnection::OnLinkReady, weak_factory_.GetWeakPtr(),
                       ip_properties));
  }
}

void L2TPConnection::OnDisconnect() {
  // TODO(b/165170125): Terminate the connection before stopping xl2tpd.
  external_task_ = nullptr;

  if (state() == State::kDisconnecting) {
    NotifyStopped();
  }
}

bool L2TPConnection::WritePPPDConfig() {
  pppd_config_path_ = temp_dir_.GetPath().Append(kPPPDConfigFileName);

  // TODO(b/200636771): Use proper mtu and mru.
  std::vector<std::string> lines = {
      "ipcp-accept-local",
      "ipcp-accept-remote",
      "refuse-eap",
      "noccp",
      "noauth",
      "crtscts",
      "mtu 1410",
      "mru 1410",
      "lock",
      "connect-delay 5000",
      "nodefaultroute",
      "nosystemconfig",
      "usepeerdns",
  };
  if (config_->lcp_echo) {
    lines.push_back("lcp-echo-failure 4");
    lines.push_back("lcp-echo-interval 30");
  }

  lines.push_back(base::StrCat({"plugin ", PPPDaemon::kShimPluginPath}));

  std::string contents = base::JoinString(lines, "\n");
  return vpn_util_->WriteConfigFile(pppd_config_path_, contents);
}

bool L2TPConnection::WriteL2TPDConfig() {
  l2tpd_config_path_ = temp_dir_.GetPath().Append(kL2TPDConfigFileName);

  // TODO(b/165170125): Fill in contents.

  return vpn_util_->WriteConfigFile(l2tpd_config_path_, "");
}

void L2TPConnection::StartXl2tpd() {
  const base::FilePath l2tpd_control_path =
      temp_dir_.GetPath().Append(kL2TPDControlFileName);

  std::vector<std::string> args = {
      "-c", l2tpd_config_path_.value(), "-C", l2tpd_control_path.value(),
      "-D"  // prevents xl2tpd from detaching from the terminal and daemonizing
  };

  // TODO(b/165170125): Add remote IP here.
  std::map<std::string, std::string> env = {
      {kLnsAddress, ""},
  };

  auto external_task_local = std::make_unique<ExternalTask>(
      control_interface_, process_manager_, weak_factory_.GetWeakPtr(),
      base::BindRepeating(&L2TPConnection::OnXl2tpdExitedUnexpectedly,
                          weak_factory_.GetWeakPtr()));

  Error error;
  constexpr uint64_t kCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  if (!external_task_local->StartInMinijail(
          base::FilePath(kXl2tpdPath), &args, env, VPNUtil::kVPNUser,
          VPNUtil::kVPNGroup, kCapMask, /*inherit_supplementary_groups=*/true,
          /*close_nonstd_fds=*/true, &error)) {
    NotifyFailure(Service::kFailureInternal,
                  base::StrCat({"Failed to start xl2tpd: ", error.message()}));
    return;
  }

  external_task_ = std::move(external_task_local);
}

void L2TPConnection::OnLinkReady(const IPConfig::Properties& ip_properties,
                                 const std::string& if_name,
                                 int if_index) {
  if (state() != State::kConnecting) {
    // Needs to do nothing here. The ppp interface is managed by the pppd
    // process so we don't need to remove it here.
    LOG(WARNING) << "OnLinkReady() called but the current state is " << state();
    return;
  }
  NotifyConnected(if_name, if_index, ip_properties);
}

void L2TPConnection::OnXl2tpdExitedUnexpectedly(pid_t pid, int exit_code) {
  const std::string message =
      base::StringPrintf("xl2tpd exited unexpectedly with code=%d", exit_code);
  if (!IsConnectingOrConnected()) {
    LOG(WARNING) << message;
    return;
  }
  NotifyFailure(Service::kFailureInternal, message);
}

}  // namespace shill
