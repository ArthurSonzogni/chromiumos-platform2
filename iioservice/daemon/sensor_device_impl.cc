// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_device_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/strings/string_util.h>
#include <libmems/common_types.h>
#include <libmems/iio_channel.h>

#include "iioservice/include/common.h"

namespace iioservice {

// static
void SensorDeviceImpl::SensorDeviceImplDeleter(SensorDeviceImpl* device) {
  if (device == nullptr)
    return;

  if (!device->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    device->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorDeviceImpl::SensorDeviceImplDeleter, device));
    return;
  }

  delete device;
}

// static
SensorDeviceImpl::ScopedSensorDeviceImpl SensorDeviceImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    libmems::IioContext* context) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  ScopedSensorDeviceImpl device(nullptr, SensorDeviceImplDeleter);

  std::unique_ptr<base::Thread> thread(new base::Thread("SensorDeviceImpl"));
  if (!thread->StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOGF(ERROR) << "Failed to start thread with TYPE_IO";
    device.reset();
    return device;
  }

  device.reset(new SensorDeviceImpl(std::move(ipc_task_runner), context,
                                    std::move(thread)));

  return device;
}

SensorDeviceImpl::~SensorDeviceImpl() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  samples_handlers_.clear();
  sample_thread_->Stop();
  receiver_set_.Clear();
  clients_.clear();
}

void SensorDeviceImpl::AddReceiver(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> request,
    const std::set<cros::mojom::DeviceType>& types) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (!context_->IsValid()) {
    LOGF(ERROR) << "No devices in the context. Failed to register to device "
                   "with iio_device_id: "
                << iio_device_id;
    return;
  }

  auto iio_device = context_->GetDeviceById(iio_device_id);
  if (!iio_device) {
    LOGF(ERROR) << "Invalid iio_device_id: " << iio_device_id;
    return;
  }

  mojo::ReceiverId id =
      receiver_set_.Add(this, std::move(request), ipc_task_runner_);

  clients_.emplace(id, ClientData(id, iio_device, types));
}

void SensorDeviceImpl::SetTimeout(uint32_t timeout) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;

  it->second.timeout = timeout;
}

void SensorDeviceImpl::GetAttributes(const std::vector<std::string>& attr_names,
                                     GetAttributesCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;

  std::vector<base::Optional<std::string>> values;
  values.reserve(attr_names.size());
  for (const auto& attr_name : attr_names) {
    auto value_opt = client.iio_device->ReadStringAttribute(attr_name);
    if (value_opt.has_value()) {
      value_opt =
          base::TrimString(value_opt.value(), base::StringPiece("\0\n", 2),
                           base::TRIM_TRAILING)
              .as_string();
    }

    values.push_back(std::move(value_opt));
  }

  std::move(callback).Run(std::move(values));
}

void SensorDeviceImpl::SetFrequency(double frequency,
                                    SetFrequencyCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.iio_device);
  if (it_handler != samples_handlers_.end()) {
    it_handler->second->UpdateFrequency(&client, frequency,
                                        std::move(callback));
    return;
  }

  client.frequency = frequency;
  std::move(callback).Run(frequency);
}

void SensorDeviceImpl::StartReadingSamples(
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;

  if (samples_handlers_.find(client.iio_device) == samples_handlers_.end()) {
    SamplesHandler::ScopedSamplesHandler handler = {
        nullptr, SamplesHandler::SamplesHandlerDeleter};

    handler = SamplesHandler::Create(
        ipc_task_runner_, sample_thread_->task_runner(), client.iio_device);

    if (!handler) {
      LOGF(ERROR) << "Failed to create the samples handler for device: "
                  << client.iio_device->GetId();
      return;
    }

    samples_handlers_.emplace(client.iio_device, std::move(handler));
  }

  samples_handlers_.at(client.iio_device)
      ->AddClient(&client, std::move(observer));
}

void SensorDeviceImpl::StopReadingSamples() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  StopReadingSamplesOnClient(id);
}

void SensorDeviceImpl::GetAllChannelIds(GetAllChannelIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  auto iio_device = it->second.iio_device;
  std::vector<std::string> chn_ids;
  for (auto iio_channel : iio_device->GetAllChannels())
    chn_ids.push_back(iio_channel->GetId());

  std::move(callback).Run(std::move(chn_ids));
}

void SensorDeviceImpl::SetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    SetChannelsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.iio_device);
  if (it_handler != samples_handlers_.end()) {
    it_handler->second->UpdateChannelsEnabled(
        &client, std::move(iio_chn_indices), en, std::move(callback));
    return;
  }

  if (en) {
    for (int32_t chn_index : iio_chn_indices)
      client.enabled_chn_indices.emplace(chn_index);
  } else {
    for (int32_t chn_index : iio_chn_indices)
      client.enabled_chn_indices.erase(chn_index);
  }

  std::move(callback).Run({});
}

void SensorDeviceImpl::GetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    GetChannelsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.iio_device);
  if (it_handler != samples_handlers_.end()) {
    it_handler->second->GetChannelsEnabled(&client, std::move(iio_chn_indices),
                                           std::move(callback));
    return;
  }

  // List of channels failed to enabled.
  std::vector<bool> enabled;

  for (int32_t chn_index : iio_chn_indices) {
    enabled.push_back(client.enabled_chn_indices.find(chn_index) !=
                      client.enabled_chn_indices.end());
  }

  std::move(callback).Run(std::move(enabled));
}

void SensorDeviceImpl::GetChannelsAttributes(
    const std::vector<int32_t>& iio_chn_indices,
    const std::string& attr_name,
    GetChannelsAttributesCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end())
    return;
  ClientData& client = it->second;
  auto iio_device = client.iio_device;

  std::vector<base::Optional<std::string>> values;

  for (int32_t chn_index : iio_chn_indices) {
    auto chn = iio_device->GetChannel(chn_index);

    if (!chn) {
      LOG(ERROR) << "Cannot find chn with index: " << chn_index;
      values.push_back(base::nullopt);
      continue;
    }

    base::Optional<std::string> value_opt = chn->ReadStringAttribute(attr_name);
    if (value_opt.has_value()) {
      value_opt =
          base::TrimString(value_opt.value(), base::StringPiece("\0\n", 2),
                           base::TRIM_TRAILING)
              .as_string();
    }

    values.push_back(value_opt);
  }

  std::move(callback).Run(std::move(values));
}

SensorDeviceImpl::SensorDeviceImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    libmems::IioContext* context,
    std::unique_ptr<base::Thread> thread)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      context_(std::move(context)),
      sample_thread_(std::move(thread)) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &SensorDeviceImpl::OnSensorDeviceDisconnect, weak_factory_.GetWeakPtr()));
}

void SensorDeviceImpl::OnSensorDeviceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();

  LOGF(INFO) << "SensorDevice disconnected. ReceiverId: " << id;
  StopReadingSamplesOnClient(id);

  // Run RemoveClient(id) on |sample_thread_| so that tasks to remove the client
  // in SamplesHandler have been done.
  sample_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImpl::RemoveClient,
                                base::Unretained(this), id));
}

void SensorDeviceImpl::RemoveClient(mojo::ReceiverId id) {
  DCHECK(sample_thread_->task_runner()->RunsTasksInCurrentSequence());

  clients_.erase(id);
}

void SensorDeviceImpl::StopReadingSamplesOnClient(mojo::ReceiverId id) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto it = clients_.find(id);
  if (it == clients_.end())
    return;

  ClientData& client = it->second;

  if (samples_handlers_.find(client.iio_device) != samples_handlers_.end())
    samples_handlers_.at(client.iio_device)->RemoveClient(&client);
}

}  // namespace iioservice
