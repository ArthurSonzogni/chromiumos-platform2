// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_DAEMON_H_
#define IIOSERVICE_DAEMON_DAEMON_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <dbus/exported_object.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"
#include "iioservice/libiioservice_ipc/sensor_server_dbus.h"

namespace iioservice {

class Daemon : public brillo::DBusDaemon, public SensorServerDbus {
 public:
  ~Daemon() override;

 protected:
  // brillo::DBusDaemon:
  int OnInit() override;

 private:
  // This function initializes the D-Bus service. The primary function of the
  // D-Bus interface is to get notified by mems_setup that a device is ready to
  // be used.
  void InitDBus();

  // Method called when kMemsSetupDoneMethod is received from mems_setup.
  // Handles reporting of a device is setup by mems_setup and ready to be used.
  void HandleMemsSetupDone(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender);
  // Method called when kMemsRemoveDoneMethod is received from mems_setup.
  // Handles reporting of a device is setup by mems_setup and ready to be used.
  void HandleMemsRemoveDone(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender);

  // SensorServerDbus overrides:
  void OnServerReceived(
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server) override;

  void OnMojoDisconnect();

  // IPC Support
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  SensorHalServerImpl::ScopedSensorHalServerImpl sensor_hal_server_ = {
      nullptr, SensorHalServerImpl::SensorHalServerImplDeleter};

  // Must be last class member.
  base::WeakPtrFactory<Daemon> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_DAEMON_H_
