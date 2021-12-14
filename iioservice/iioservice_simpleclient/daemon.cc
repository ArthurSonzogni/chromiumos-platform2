// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <mojo/core/embedder/embedder.h>

#include "iioservice/include/common.h"

namespace iioservice {

Daemon::~Daemon() = default;

Daemon::Daemon() = default;

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

  SetSensorClient();

  return exit_code;
}

void Daemon::OnClientReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  sensor_client_->BindClient(std::move(client));
}

void Daemon::OnMojoDisconnect() {
  LOGF(INFO) << "Quitting this process.";
  Quit();
}

}  // namespace iioservice
