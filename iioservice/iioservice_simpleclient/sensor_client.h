// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_SENSOR_CLIENT_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_SENSOR_CLIENT_H_

#include <memory>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "iioservice/iioservice_simpleclient/common.h"
#include "iioservice/mojo/cros_sensor_service.mojom.h"
#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

class SensorClient : public cros::mojom::SensorHalClient {
 public:
  using OnMojoDisconnectCallback = base::RepeatingCallback<void(bool)>;
  using QuitCallback = base::OnceCallback<void()>;

  static void SensorClientDeleter(SensorClient* observer);

  using ScopedSensorClient =
      std::unique_ptr<SensorClient, decltype(&SensorClientDeleter)>;

  void BindClient(mojo::PendingReceiver<cros::mojom::SensorHalClient> client);

  // cros::mojom::SensorHalClient overrides:
  void SetUpChannel(
      mojo::PendingRemote<cros::mojom::SensorService> pending_remote) override;

 protected:
  SensorClient(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
               OnMojoDisconnectCallback on_mojo_disconnect_callback,
               QuitCallback quit_callback);

  void SetUpChannelTimeout();

  virtual void Start() = 0;
  virtual void Reset();

  void Quit();
  void OnMojoDisconnect(bool mojo_broker);

  void OnClientDisconnect();
  void OnServiceDisconnect();

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  OnMojoDisconnectCallback on_mojo_disconnect_callback_;
  QuitCallback quit_callback_;

  mojo::Receiver<cros::mojom::SensorHalClient> client_{this};
  mojo::Remote<cros::mojom::SensorService> sensor_service_remote_;

  bool sensor_service_setup_ = false;

  std::unique_ptr<TimeoutDelegate> timeout_delegate_;

  base::WeakPtrFactory<SensorClient> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_SENSOR_CLIENT_H_
