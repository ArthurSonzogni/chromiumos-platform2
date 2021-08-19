// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path_watcher.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "shill/process_manager.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

namespace {

constexpr char kBaseRunDir[] = "/run/ipsec";
constexpr char kStrongSwanConfFileName[] = "strongswan.conf";
constexpr char kCharonPath[] = "/usr/libexec/ipsec/charon";
constexpr char kViciSocketPath[] = "/run/ipsec/charon.vici";
constexpr char kSmartcardModuleName[] = "crypto_module";

}  // namespace

IPsecConnection::IPsecConnection(std::unique_ptr<Config> config,
                                 std::unique_ptr<Callbacks> callbacks,
                                 EventDispatcher* dispatcher,
                                 ProcessManager* process_manager)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)),
      vici_socket_path_(kViciSocketPath),
      process_manager_(process_manager),
      vpn_util_(VPNUtil::New()) {}

IPsecConnection::~IPsecConnection() {
  if (state() == State::kIdle || state() == State::kStopped) {
    return;
  }

  // This is unexpected but cannot be fully avoided. Call OnDisconnect() to make
  // sure resources are released.
  LOG(WARNING) << "Destructor called but the current state is " << state();
  OnDisconnect();
}

void IPsecConnection::OnConnect() {
  temp_dir_ = vpn_util_->CreateScopedTempDir(base::FilePath(kBaseRunDir));
  if (!temp_dir_.IsValid()) {
    NotifyFailure(Service::kFailureInternal,
                  "Failed to create temp dir for IPsec");
    return;
  }

  ScheduleConnectTask(ConnectStep::kStart);
}

void IPsecConnection::ScheduleConnectTask(ConnectStep step) {
  switch (step) {
    case ConnectStep::kStart:
      WriteStrongSwanConfig();
      return;
    case ConnectStep::kStrongSwanConfigWritten:
      StartCharon();
      return;
    case ConnectStep::kCharonStarted:
      WriteSwanctlConfig();
      return;
    case ConnectStep::kSwanctlConfigWritten:
      SwanctlLoadConfig();
      return;
    case ConnectStep::kSwanctlConfigLoaded:
      SwanctlInitiateConnection();
      return;
    case ConnectStep::kIPsecConnected:
      // TODO(b/165170125): Start L2TP here.
      return;
    default:
      NOTREACHED();
  }
}

void IPsecConnection::WriteStrongSwanConfig() {
  strongswan_conf_path_ = temp_dir_.GetPath().Append(kStrongSwanConfFileName);

  // See the following link for the format and descriptions for each field:
  // https://wiki.strongswan.org/projects/strongswan/wiki/strongswanconf
  // TODO(b/165170125): Check if routing_table is still required.
  std::vector<std::string> lines = {
      "charon {",
      "  accept_unencrypted_mainmode_messages = yes",
      "  ignore_routing_tables = 0",
      "  install_routes = no",
      "  routing_table = 0",
      "  syslog {",
      "    daemon {",
      "      ike = 2",  // Logs some traffic selector info.
      "      cfg = 2",  // Logs algorithm proposals.
      "      knl = 2",  // Logs high-level xfrm crypto parameters.
      "    }",
      "  }",
      "  plugins {",
      "    pkcs11 {",
      "      modules {",
      base::StringPrintf("        %s {", kSmartcardModuleName),
      "          path = " + std::string{PKCS11_LIB},
      "        }",
      "      }",
      "    }",
      "  }",
      "}",
  };

  std::string contents = base::JoinString(lines, "\n");
  if (!vpn_util_->WriteConfigFile(strongswan_conf_path_, contents)) {
    NotifyFailure(Service::kFailureInternal,
                  base::StrCat({"Failed to write ", kStrongSwanConfFileName}));
    return;
  }
  ScheduleConnectTask(ConnectStep::kStrongSwanConfigWritten);
}

void IPsecConnection::WriteSwanctlConfig() {
  // TODO(b/165170125): Implement WriteSwanctlConfig().
  ScheduleConnectTask(ConnectStep::kSwanctlConfigWritten);
}

void IPsecConnection::StartCharon() {
  // TODO(b/165170125): Check the behavior when shill crashes (if charon is
  // still running).
  // TODO(b/165170125): May need to increase RLIMIT_AS to run charon. See
  // https://crrev.com/c/1757203.
  std::vector<std::string> args = {};
  std::map<std::string, std::string> env = {
      {"STRONGSWAN_CONF", strongswan_conf_path_.value()},
  };
  // TODO(b/197199752): Consider removing CAP_SETGID.
  constexpr uint64_t kCapMask =
      CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
      CAP_TO_MASK(CAP_NET_RAW) | CAP_TO_MASK(CAP_SETGID);
  charon_pid_ = process_manager_->StartProcessInMinijail(
      FROM_HERE, base::FilePath(kCharonPath), args, env, VPNUtil::kVPNUser,
      VPNUtil::kVPNGroup, kCapMask,
      /*inherit_supplementary_groups=*/true, /*close_nonstd_fds*/ true,
      base::BindRepeating(&IPsecConnection::OnCharonExitedUnexpectedly,
                          weak_factory_.GetWeakPtr()));

  if (charon_pid_ == -1) {
    NotifyFailure(Service::kFailureInternal, "Failed to start charon");
    return;
  }

  LOG(INFO) << "charon started";

  if (!base::PathExists(vici_socket_path_)) {
    vici_socket_watcher_ = std::make_unique<base::FilePathWatcher>();
    auto callback = base::BindRepeating(&IPsecConnection::OnViciSocketPathEvent,
                                        weak_factory_.GetWeakPtr());
    if (!vici_socket_watcher_->Watch(vici_socket_path_,
                                     base::FilePathWatcher::Type::kNonRecursive,
                                     callback)) {
      NotifyFailure(Service::kFailureInternal,
                    "Failed to set up FilePathWatcher for the vici socket");
      return;
    }
  } else {
    LOG(INFO) << "vici socket is already here";
    ScheduleConnectTask(ConnectStep::kCharonStarted);
  }
}

void IPsecConnection::SwanctlLoadConfig() {
  // TODO(b/165170125): Implement SwanctlLoadConfig().
  ScheduleConnectTask(ConnectStep::kSwanctlConfigLoaded);
}

void IPsecConnection::SwanctlInitiateConnection() {
  // TODO(b/165170125): Implement SwanctlInitiateConnection().
  ScheduleConnectTask(ConnectStep::kIPsecConnected);
}

void IPsecConnection::OnViciSocketPathEvent(const base::FilePath& /*path*/,
                                            bool error) {
  if (state() != State::kConnecting) {
    LOG(WARNING) << "OnViciSocketPathEvent triggered on state " << state();
    return;
  }

  if (error) {
    NotifyFailure(Service::kFailureInternal,
                  "FilePathWatcher error for the vici socket");
    return;
  }

  if (!base::PathExists(vici_socket_path_)) {
    // This is kind of unexpected, since the first event should be the creation
    // of this file. Waits for the next event.
    LOG(WARNING) << "vici socket is still not ready";
    return;
  }

  LOG(INFO) << "vici socket is ready";

  vici_socket_watcher_ = nullptr;
  ScheduleConnectTask(ConnectStep::kCharonStarted);
}

void IPsecConnection::OnCharonExitedUnexpectedly(int exit_code) {
  charon_pid_ = -1;
  NotifyFailure(Service::kFailureInternal,
                base::StringPrintf(
                    "charon exited unexpectedly with exit code %d", exit_code));
  return;
}

void IPsecConnection::OnDisconnect() {
  // TODO(b/165170125): Implement OnDisconnect().
  if (charon_pid_ != -1) {
    process_manager_->StopProcess(charon_pid_);
  }
}

}  // namespace shill
