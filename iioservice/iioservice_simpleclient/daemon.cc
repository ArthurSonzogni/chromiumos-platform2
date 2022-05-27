// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <mojo/core/embedder/embedder.h>

namespace iioservice {

namespace {

constexpr int kMojoBootstrapTimeoutInMilliseconds = 10000;
constexpr const char kMojoBootstrapTimeoutLog[] =
    "Daemon is not bootstrapped to the mojo network";

constexpr int kMojoDisconnectTimeoutInMilliseconds = 5000;
constexpr const char kMojoDisconnectTimeoutLog[] =
    "Mojo broker didn't disconnect";

}  // namespace

Daemon::~Daemon() = default;

Daemon::Daemon(int mojo_broker_disconnect_tolerance)
    : mojo_broker_disconnect_tolerance_(mojo_broker_disconnect_tolerance) {}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  SetBus(bus_.get());
  BootstrapMojoConnection();
  SetMojoBootstrapTimeout();

  SetSensorClient();

  return exit_code;
}

void Daemon::OnClientReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  timeout_delegate_.reset();
  sensor_client_->BindClient(std::move(client));
}

void Daemon::OnMojoDisconnect(bool mojo_broker) {
  if (mojo_broker_disconnect_tolerance_ == 0) {
    Quit();
    return;
  }

  if (!mojo_broker) {
    DCHECK(!timeout_delegate_);
    timeout_delegate_.reset(new TimeoutDelegate(
        kMojoDisconnectTimeoutInMilliseconds, kMojoDisconnectTimeoutLog,
        base::BindOnce(&Daemon::Quit, base::Unretained(this))));
    return;
  }

  timeout_delegate_.reset();
  --mojo_broker_disconnect_tolerance_;
  ReconnectMojoWithDelay();
  SetMojoBootstrapTimeout();
}

void Daemon::SetMojoBootstrapTimeout() {
  timeout_delegate_.reset(new TimeoutDelegate(
      kMojoBootstrapTimeoutInMilliseconds, kMojoBootstrapTimeoutLog,
      base::BindOnce(&Daemon::Quit, base::Unretained(this))));
}

}  // namespace iioservice
