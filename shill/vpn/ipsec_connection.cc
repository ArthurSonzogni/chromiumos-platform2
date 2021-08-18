// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <utility>

namespace shill {

IPsecConnection::IPsecConnection(std::unique_ptr<Config> config,
                                 std::unique_ptr<Callbacks> callbacks,
                                 EventDispatcher* dispatcher)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)) {}

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
  // TODO(b/165170125): Implement WriteStrongSwanConfig().
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
