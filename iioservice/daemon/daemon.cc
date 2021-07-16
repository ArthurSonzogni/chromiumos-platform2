// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/invitation.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"
#include "iioservice/daemon/sensor_metrics.h"
#include "iioservice/include/common.h"
#include "iioservice/include/dbus-constants.h"

namespace iioservice {

Daemon::~Daemon() {
  SensorMetrics::Shutdown();
}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  SensorMetrics::Initialize();

  InitDBus();

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  SetBus(bus_.get());
  BootstrapMojoConnection();

  return 0;
}

void Daemon::InitDBus() {
  dbus::ExportedObject* const iioservice_exported_object =
      bus_->GetExportedObject(
          dbus::ObjectPath(::iioservice::kIioserviceServicePath));
  CHECK(iioservice_exported_object);

  // Register a handler of the MemsSetupDone method.
  CHECK(iioservice_exported_object->ExportMethodAndBlock(
      ::iioservice::kIioserviceInterface, ::iioservice::kMemsSetupDoneMethod,
      base::BindRepeating(&Daemon::HandleMemsSetupDone,
                          weak_factory_.GetWeakPtr())));

  // Take ownership of the ML service.
  CHECK(bus_->RequestOwnershipAndBlock(::iioservice::kIioserviceServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
}

void Daemon::HandleMemsSetupDone(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  if (sensor_hal_server_) {
    dbus::MessageReader reader(method_call);
    int32_t iio_device_id;
    if (!reader.PopInt32(&iio_device_id) || iio_device_id < 0) {
      LOGF(ERROR) << "Couldn't extract iio_device_id (int32_t) from D-Bus call";
      std::move(response_sender)
          .Run(dbus::ErrorResponse::FromMethodCall(
              method_call, DBUS_ERROR_FAILED,
              "Couldn't extract iio_device_id (int32_t)"));
      return;
    }

    sensor_hal_server_->OnDeviceAdded(iio_device_id);
  }

  // Send success response.
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
}

void Daemon::OnServerReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalServer> server) {
  sensor_hal_server_ = SensorHalServerImpl::Create(
      base::ThreadTaskRunnerHandle::Get(), std::move(server),
      base::BindOnce(&Daemon::OnMojoDisconnect, weak_factory_.GetWeakPtr()));
}

void Daemon::OnMojoDisconnect() {
  LOGF(WARNING) << "Chromium crashed. Try to establish a new Mojo connection.";
  sensor_hal_server_.reset();
  ReconnectMojoWithDelay();
}

}  // namespace iioservice
