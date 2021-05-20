// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_SENSOR_HAL_SERVER_IMPL_H_
#define IIOSERVICE_DAEMON_SENSOR_HAL_SERVER_IMPL_H_

#include <memory>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "iioservice/daemon/sensor_service_impl.h"
#include "iioservice/mojo/cros_sensor_service.mojom.h"

namespace iioservice {

class SensorHalServerImpl : public cros::mojom::SensorHalServer {
 public:
  static void SensorHalServerImplDeleter(SensorHalServerImpl* server);
  using ScopedSensorHalServerImpl =
      std::unique_ptr<SensorHalServerImpl,
                      decltype(&SensorHalServerImplDeleter)>;

  using MojoOnFailureCallback = base::OnceCallback<void()>;

  // Should be used on |ipc_task_runner|.
  static ScopedSensorHalServerImpl Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
      MojoOnFailureCallback mojo_on_failure_callback);

  // cros::mojom::SensorHalServer overrides:
  void CreateChannel(mojo::PendingReceiver<cros::mojom::SensorService>
                         sensor_service_request) override;

  void OnDeviceAdded(int iio_device_id);

 protected:
  SensorHalServerImpl(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
      MojoOnFailureCallback mojo_on_failure_callback);

  virtual void SetSensorService();

  void OnSensorHalServerError();

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  mojo::Receiver<cros::mojom::SensorHalServer> receiver_;
  MojoOnFailureCallback mojo_on_failure_callback_;

  SensorServiceImpl::ScopedSensorServiceImpl sensor_service_ = {
      nullptr, SensorServiceImpl::SensorServiceImplDeleter};

  base::WeakPtrFactory<SensorHalServerImpl> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_SENSOR_HAL_SERVER_IMPL_H_
