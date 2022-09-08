// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/sensor_client.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr int kSetUpChannelTimeoutInMilliseconds = 3000;
constexpr const char kSetUpChannelTimeoutLog[] =
    "SensorService to iioservice is not received";

}  // namespace

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

  timeout_delegate_.reset(new TimeoutDelegate(
      kSetUpChannelTimeoutInMilliseconds, kSetUpChannelTimeoutLog,
      base::BindOnce(&SensorClient::Reset, weak_factory_.GetWeakPtr())));
}

void SensorClient::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> pending_remote) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!sensor_service_remote_.is_bound());

  timeout_delegate_.reset();

  sensor_service_setup_ = true;

  sensor_service_remote_.Bind(std::move(pending_remote));
  sensor_service_remote_.set_disconnect_handler(base::BindOnce(
      &SensorClient::OnServiceDisconnect, weak_factory_.GetWeakPtr()));

  Start();
}

SensorClient::SensorClient(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    OnMojoDisconnectCallback on_mojo_disconnect_callback,
    QuitCallback quit_callback)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      on_mojo_disconnect_callback_(std::move(on_mojo_disconnect_callback)),
      quit_callback_(std::move(quit_callback)) {}

void SensorClient::SetUpChannelTimeout() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (sensor_service_setup_)
    return;

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(ERROR) << "SetUpChannelTimeout";
  Reset();
}

void SensorClient::Reset() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_service_remote_.reset();

  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void SensorClient::OnMojoDisconnect(bool mojo_broker) {
  auto quit_callback = std::move(quit_callback_);
  Reset();
  quit_callback_ = std::move(quit_callback);

  on_mojo_disconnect_callback_.Run(mojo_broker);
}

void SensorClient::OnClientDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorHalClient disconnected";

  client_.reset();
  OnMojoDisconnect(true);
}

void SensorClient::OnServiceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorService disconnected";

  OnMojoDisconnect(false);
}

}  // namespace iioservice
