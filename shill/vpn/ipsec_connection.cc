// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/strings/strcat.h>

#include "shill/vpn/vpn_util.h"

namespace shill {

namespace {

constexpr char kBaseRunDir[] = "/run/ipsec";
constexpr char kStrongSwanConfFileName[] = "strongswan.conf";
constexpr char kSmartcardModuleName[] = "crypto_module";

}  // namespace

IPsecConnection::IPsecConnection(std::unique_ptr<Config> config,
                                 std::unique_ptr<Callbacks> callbacks,
                                 EventDispatcher* dispatcher)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)),
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
  // TODO(b/165170125): Implement StartCharon().
  ScheduleConnectTask(ConnectStep::kCharonStarted);
}

void IPsecConnection::SwanctlLoadConfig() {
  // TODO(b/165170125): Implement SwanctlLoadConfig().
  ScheduleConnectTask(ConnectStep::kSwanctlConfigLoaded);
}

void IPsecConnection::SwanctlInitiateConnection() {
  // TODO(b/165170125): Implement SwanctlInitiateConnection().
  ScheduleConnectTask(ConnectStep::kIPsecConnected);
}

void IPsecConnection::OnDisconnect() {
  // TODO(b/165170125): Implement OnDisconnect().
}

}  // namespace shill
