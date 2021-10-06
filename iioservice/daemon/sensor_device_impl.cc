// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_device_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/containers/contains.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <libmems/common_types.h>
#include <libmems/iio_channel.h>

#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr char kDeviceRemovedDescription[] = "Device was removed";

const std::vector<cros::mojom::DeviceType> kMotionSensors = {
    cros::mojom::DeviceType::ACCEL, cros::mojom::DeviceType::ANGLVEL,
    cros::mojom::DeviceType::MAGN};

constexpr char kChannelAttributeFormat[] = "in_%s_%s";

}  // namespace

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

void SensorDeviceImpl::OnDeviceAdded(
    libmems::IioDevice* iio_device,
    const std::set<cros::mojom::DeviceType>& types) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  devices_.emplace(iio_device->GetId(), DeviceData(iio_device, types));
}

void SensorDeviceImpl::OnDeviceRemoved(int iio_device_id) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  for (auto it = clients_.begin(); it != clients_.end();) {
    if (it->second.device_data->iio_device->GetId() == iio_device_id) {
      auto it_handler =
          samples_handlers_.find(it->second.device_data->iio_device);
      if (it_handler != samples_handlers_.end()) {
        it_handler->second->ResetWithReason(
            cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
            kDeviceRemovedDescription);
        samples_handlers_.erase(it_handler);
      }

      receiver_set_.RemoveWithReason(
          it->first,
          static_cast<uint32_t>(
              cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED),
          kDeviceRemovedDescription);
      it = clients_.erase(it);
    } else {
      ++it;
    }
  }

  devices_.erase(iio_device_id);
}

void SensorDeviceImpl::AddReceiver(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto it = devices_.find(iio_device_id);
  if (it == devices_.end()) {
    LOGF(ERROR) << "Invalid iio_device_id: " << iio_device_id;
    return;
  }

  mojo::ReceiverId id =
      receiver_set_.Add(this, std::move(request), ipc_task_runner_);

  clients_.emplace(id, ClientData(id, &it->second));
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run(std::vector<base::Optional<std::string>>(
        attr_names.size(), base::nullopt));
    return;
  }

  ClientData& client = it->second;

  std::vector<base::Optional<std::string>> values;
  values.reserve(attr_names.size());
  for (const auto& attr_name : attr_names) {
    base::Optional<std::string> value_opt;
    if (attr_name == cros::mojom::kSysPath) {
      auto path_opt = GetAbsoluteSysPath(client.device_data->iio_device);
      if (path_opt.has_value())
        value_opt = path_opt.value().value();
    } else {
      value_opt =
          client.device_data->iio_device->ReadStringAttribute(attr_name);
    }

    if (!value_opt.has_value()) {
      // Look for channels' attributes instead.
      for (auto type : client.device_data->types) {
        auto type_in_string = DeviceTypeToString(type);
        if (!type_in_string.has_value())
          continue;

        value_opt = client.device_data->iio_device->ReadStringAttribute(
            base::StringPrintf(kChannelAttributeFormat, type_in_string->c_str(),
                               attr_name.c_str()));

        if (value_opt.has_value())
          break;
      }
    }

    if (value_opt.has_value()) {
      value_opt = std::string(base::TrimString(value_opt.value(),
                                               base::StringPiece("\0\n", 2),
                                               base::TRIM_TRAILING));
    } else {
      if (attr_name == cros::mojom::kLocation) {
        base::Optional<cros::mojom::DeviceType> type;
        for (auto& t : kMotionSensors) {
          if (base::Contains(client.device_data->types, t)) {
            type = t;
            break;
          }
        }

        if (type.has_value()) {
          base::Optional<int32_t> only_device_id;
          for (auto& device : devices_) {
            if (base::Contains(device.second.types, type.value()) &&
                device.second.on_dut) {
              if (!only_device_id.has_value()) {
                only_device_id = device.first;
              } else {
                only_device_id = base::nullopt;
                break;
              }
            }
          }

          if (only_device_id.has_value() &&
              only_device_id == client.device_data->iio_device->GetId()) {
            // It's the only motion sensor type on dut. Assume it on location
            // lid.
            value_opt = cros::mojom::kLocationLid;
          }
        }
      }
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run(-1.0);
    return;
  }

  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.device_data->iio_device);
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    return;
  }

  ClientData& client = it->second;

  if (samples_handlers_.find(client.device_data->iio_device) ==
      samples_handlers_.end()) {
    SamplesHandler::ScopedSamplesHandler handler = {
        nullptr, SamplesHandler::SamplesHandlerDeleter};

    handler =
        SamplesHandler::Create(ipc_task_runner_, sample_thread_->task_runner(),
                               client.device_data->iio_device);

    if (!handler) {
      LOGF(ERROR) << "Failed to create the samples handler for device: "
                  << client.device_data->iio_device->GetId();
      return;
    }

    samples_handlers_.emplace(client.device_data->iio_device,
                              std::move(handler));
  }

  samples_handlers_.at(client.device_data->iio_device)
      ->AddClient(&client, std::move(observer));
}

void SensorDeviceImpl::StopReadingSamples() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  StopReadingSamplesOnClient(id, base::DoNothing());
}

void SensorDeviceImpl::GetAllChannelIds(GetAllChannelIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run({});
    return;
  }

  auto iio_device = it->second.device_data->iio_device;
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run(iio_chn_indices);
    return;
  }

  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.device_data->iio_device);
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run(std::vector<bool>(iio_chn_indices.size(), false));
    return;
  }

  ClientData& client = it->second;

  auto it_handler = samples_handlers_.find(client.device_data->iio_device);
  if (it_handler != samples_handlers_.end()) {
    it_handler->second->GetChannelsEnabled(&client, std::move(iio_chn_indices),
                                           std::move(callback));
    return;
  }

  // List of channels enabled.
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
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run(std::vector<base::Optional<std::string>>(
        iio_chn_indices.size(), base::nullopt));
    return;
  }

  ClientData& client = it->second;
  auto iio_device = client.device_data->iio_device;

  std::vector<base::Optional<std::string>> values;

  for (int32_t chn_index : iio_chn_indices) {
    auto chn = iio_device->GetChannel(chn_index);

    if (!chn) {
      LOGF(ERROR) << "Cannot find chn with index: " << chn_index;
      values.push_back(base::nullopt);
      continue;
    }

    base::Optional<std::string> value_opt = chn->ReadStringAttribute(attr_name);
    if (value_opt.has_value()) {
      value_opt = std::string(base::TrimString(value_opt.value(),
                                               base::StringPiece("\0\n", 2),
                                               base::TRIM_TRAILING));
    }

    values.push_back(value_opt);
  }

  std::move(callback).Run(std::move(values));
}

base::WeakPtr<SensorDeviceImpl> SensorDeviceImpl::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
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
      &SensorDeviceImpl::OnSensorDeviceDisconnect, GetWeakPtr()));
}

void SensorDeviceImpl::OnSensorDeviceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();

  LOGF(INFO) << "SensorDevice disconnected. ReceiverId: " << id;
  // Run RemoveClient(id) after removing the client from SamplesHandler.
  StopReadingSamplesOnClient(id,
                             base::BindOnce(&SensorDeviceImpl::RemoveClient,
                                            weak_factory_.GetWeakPtr(), id));
}

void SensorDeviceImpl::RemoveClient(mojo::ReceiverId id) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  clients_.erase(id);
}

void SensorDeviceImpl::StopReadingSamplesOnClient(mojo::ReceiverId id,
                                                  base::OnceClosure callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto it = clients_.find(id);
  if (it == clients_.end()) {
    LOGF(ERROR) << "Failed to find clients with id: " << id;
    std::move(callback).Run();
    return;
  }

  ClientData& client = it->second;

  if (samples_handlers_.find(client.device_data->iio_device) !=
      samples_handlers_.end())
    samples_handlers_.at(client.device_data->iio_device)
        ->RemoveClient(&client, std::move(callback));
}

}  // namespace iioservice
