// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/sensor_client.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/include/common.h"

namespace iioservice {

// static
void SensorClient::SensorClientDeleter(SensorClient* sensor_client) {
  if (sensor_client == nullptr)
    return;

  if (!sensor_client->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    sensor_client->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorClient::SensorClientDeleter, sensor_client));
    return;
  }

  delete sensor_client;
}

void SensorClient::BindClient(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!client_.is_bound());

  client_.Bind(std::move(client));
  client_.set_disconnect_handler(base::BindOnce(
      &SensorClient::OnClientDisconnect, weak_factory_.GetWeakPtr()));
}

void SensorClient::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> pending_remote) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!sensor_service_remote_.is_bound());

  sensor_service_remote_.Bind(std::move(pending_remote));
  sensor_service_remote_.set_disconnect_handler(base::BindOnce(
      &SensorClient::OnServiceDisconnect, weak_factory_.GetWeakPtr()));

  Start();
}

SensorClient::SensorClient(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    QuitCallback quit_callback)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      quit_callback_(std::move(quit_callback)) {}

void SensorClient::SetUpChannelTimeout() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (sensor_service_remote_.is_bound())
    return;

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(ERROR) << "SetUpChannelTimeout";
  Reset();
}

void SensorClient::Reset() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  client_.reset();
  sensor_service_remote_.reset();

  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void SensorClient::OnClientDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorHalClient disconnected";
  Reset();
}

void SensorClient::OnServiceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorService disconnected";
  Reset();
}

}  // namespace iioservice
