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
  // TODO(b/165170125): Implement GetLogin().
}

void L2TPConnection::Notify(const std::string& reason,
                            const std::map<std::string, std::string>& dict) {
  // TODO(b/165170125): Implement GetLogin().
}

void L2TPConnection::OnDisconnect() {
  // TODO(b/165170125): Terminate the connection before stopping xl2tpd.
  external_task_ = nullptr;
  NotifyStopped();
}

bool L2TPConnection::WritePPPDConfig() {
  pppd_config_path_ = temp_dir_.GetPath().Append(kPPPDConfigFileName);

  // TODO(b/165170125): Fill in contents.

  return vpn_util_->WriteConfigFile(pppd_config_path_, "");
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
