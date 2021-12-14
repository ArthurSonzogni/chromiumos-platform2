// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/iioservice_simpleclient/sensor_client.h"
#include "iioservice/libiioservice_ipc/sensor_client_dbus.h"

namespace iioservice {

class Daemon : public brillo::DBusDaemon, public SensorClientDbus {
 public:
  ~Daemon() override;

 protected:
  Daemon();

  // Initializes |sensor_client_| (observer, query) that will interact with the
  // sensors as clients.
  virtual void SetSensorClient() = 0;

  // brillo::DBusDaemon overrides:
  int OnInit() override;

  // SensorClientDbus overrides:
  void OnClientReceived(
      mojo::PendingReceiver<cros::mojom::SensorHalClient> client) override;

  // Responds to Mojo disconnection by quitting the daemon.
  void OnMojoDisconnect();

  SensorClient::ScopedSensorClient sensor_client_ = {
      nullptr, SensorClient::SensorClientDeleter};

  // IPC Support
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_H_
