// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/samples_handler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <libmems/test_fakes.h>
#include <libmems/common_types.h>
#include <libmems/iio_channel.h>
#include <libmems/iio_context.h>
#include <libmems/iio_device.h>

#include "iioservice/daemon/sensor_metrics.h"
#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr char kNoBatchChannels[][10] = {"timestamp", "count"};
constexpr char kHWFifoFlushPath[] = "buffer/hwfifo_flush";

constexpr double kAcpiAlsMinFrequency = 0.1;
constexpr double kAcpiAlsMaxFrequency = 2.0;

constexpr cros::mojom::DeviceType kOnChangeDeviceTypes[] = {
    cros::mojom::DeviceType::LIGHT};

bool IsOnChangeDevice(ClientData* client_data) {
  if (!client_data->iio_device->HasFifo())
    return false;

  for (auto type : kOnChangeDeviceTypes) {
    if (client_data->types.find(type) != client_data->types.end())
      return true;
  }

  return false;
}

}  // namespace

// static
void SamplesHandler::SamplesHandlerDeleter(SamplesHandler* handler) {
  if (handler == nullptr)
    return;

  if (!handler->sample_task_runner_->BelongsToCurrentThread()) {
    handler->sample_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SamplesHandler::SamplesHandlerDeleter, handler));
    return;
  }

  delete handler;
}

// static
bool SamplesHandler::DisableBufferAndEnableChannels(
    libmems::IioDevice* iio_device) {
  if (iio_device->IsBufferEnabled() && !iio_device->DisableBuffer())
    return false;

  iio_device->EnableAllChannels();

  return true;
}

// static
SamplesHandler::ScopedSamplesHandler SamplesHandler::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioDevice* iio_device) {
  ScopedSamplesHandler handler(nullptr, SamplesHandlerDeleter);

  if (!iio_device->HasFifo() && !iio_device->GetHrtimer()) {
    LOGF(ERROR)
        << "Device " << iio_device->GetId()
        << " has neither fifo nor hrtimer. Cannot read samples from it.";
    return handler;
  }

  if (!DisableBufferAndEnableChannels(iio_device))
    return handler;

  double min_freq, max_freq;
  if (strcmp(iio_device->GetName(), "acpi-als") == 0) {
    min_freq = kAcpiAlsMinFrequency;
    max_freq = kAcpiAlsMaxFrequency;
  } else if (!iio_device->GetMinMaxFrequency(&min_freq, &max_freq)) {
    return handler;
  }

  handler.reset(new SamplesHandler(std::move(ipc_task_runner),
                                   std::move(sample_task_runner), iio_device,
                                   min_freq, max_freq));
  return handler;
}

SamplesHandler::~SamplesHandler() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  iio_device_->FreeBuffer();
  if (requested_frequency_ > 0.0 &&
      !iio_device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr, 0.0))
    LOGF(ERROR) << "Failed to set frequency";

  SensorMetrics::GetInstance()->SendSensorUsage(iio_device_->GetId(), 0.0);

  for (ClientData* client : inactive_clients_) {
    if (client->observer.is_bound()) {
      SensorMetrics::GetInstance()->SendSensorObserverClosed();
      client->observer.reset();
    }
  }

  for (auto& [client, _] : clients_map_) {
    if (client->observer.is_bound()) {
      SensorMetrics::GetInstance()->SendSensorObserverClosed();
      client->observer.reset();
    }
  }
}

void SamplesHandler::AddClient(
    ClientData* client_data,
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::AddClientOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                std::move(observer)));
}

void SamplesHandler::RemoveClient(ClientData* client_data) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::RemoveClientOnThread,
                                weak_factory_.GetWeakPtr(), client_data));
}
void SamplesHandler::UpdateFrequency(
    ClientData* client_data,
    double frequency,
    cros::mojom::SensorDevice::SetFrequencyCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::UpdateFrequencyOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                frequency, std::move(callback)));
}

void SamplesHandler::UpdateChannelsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    cros::mojom::SensorDevice::SetChannelsEnabledCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::UpdateChannelsEnabledOnThread,
                     weak_factory_.GetWeakPtr(), client_data,
                     std::move(iio_chn_indices), en, std::move(callback)));
}

void SamplesHandler::GetChannelsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    cros::mojom::SensorDevice::GetChannelsEnabledCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::GetChannelsEnabledOnThread,
                     weak_factory_.GetWeakPtr(), client_data,
                     std::move(iio_chn_indices), std::move(callback)));
}

SamplesHandler::SamplesHandler(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioDevice* iio_device,
    double min_freq,
    double max_freq)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      sample_task_runner_(std::move(sample_task_runner)),
      iio_device_(iio_device),
      dev_min_frequency_(min_freq),
      dev_max_frequency_(max_freq) {
  DCHECK_GE(dev_max_frequency_, dev_min_frequency_);

  auto channels = iio_device_->GetAllChannels();
  for (size_t i = 0; i < channels.size(); ++i) {
    for (size_t j = 0; j < base::size(kNoBatchChannels); ++j) {
      if (strcmp(channels[i]->GetId(), kNoBatchChannels[j]) == 0) {
        no_batch_chn_indices.emplace(i);
        break;
      }
    }
  }
}

void SamplesHandler::SetSampleWatcherOnThread() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK(!watcher_.get());

  // Flush the old samples in EC FIFO.
  if (iio_device_->HasFifo()) {
    if (!iio_device_->WriteStringAttribute(kHWFifoFlushPath, "1\n"))
      LOGF(ERROR) << "Failed to flush the old samples in EC FIFO";
  } else {
    DCHECK(iio_device_->GetHrtimer());
    if (!iio_device_->SetTrigger(iio_device_->GetHrtimer())) {
      LOGF(ERROR) << "Failed to set trigger";
      return;
    }
  }

  if (!iio_device_->CreateBuffer()) {
    LOGF(ERROR) << "Failed to create buffer";
    for (auto& [client_data, _] : clients_map_) {
      client_data->observer->OnErrorOccurred(
          cros::mojom::ObserverErrorType::GET_FD_FAILED);
    }

    return;
  }

  auto fd = iio_device_->GetBufferFd();
  if (!fd.has_value()) {
    LOGF(ERROR) << "Failed to get fd";
    for (auto& [client_data, _] : clients_map_) {
      client_data->observer->OnErrorOccurred(
          cros::mojom::ObserverErrorType::GET_FD_FAILED);
    }

    return;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd.value(),
      base::BindRepeating(&SamplesHandler::OnSampleAvailableWithoutBlocking,
                          weak_factory_.GetWeakPtr()));
}

void SamplesHandler::StopSampleWatcherOnThread() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  watcher_.reset();
  iio_device_->FreeBuffer();
  iio_device_->SetTrigger(nullptr);
}

void SamplesHandler::AddActiveClientOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);
  DCHECK(client_data->IsActive());
  DCHECK(client_data->observer.is_bound());
  DCHECK(inactive_clients_.find(client_data) == inactive_clients_.end());
  DCHECK(clients_map_.find(client_data) == clients_map_.end());

  clients_map_.emplace(client_data, SampleData{});
  clients_map_[client_data].sample_index = samples_cnt_;
  if (IsOnChangeDevice(client_data)) {
    // Read the first sample of the ON_CHANGE sensor for the sensor client.
    libmems::IioDevice::IioSample sample;
    for (int32_t index : client_data->enabled_chn_indices) {
      auto channel = client_data->iio_device->GetChannel(index);
      // Read from the input attribute or the raw attribute.
      auto value_opt = channel->ReadNumberAttribute(kInputAttr);
      if (!value_opt.has_value())
        value_opt = channel->ReadNumberAttribute(libmems::kRawAttr);

      if (value_opt.has_value())
        sample[index] = value_opt.value();
    }

    if (!sample.empty())
      client_data->observer->OnSampleUpdated(std::move(sample));
  }

  if (!watcher_.get())
    SetSampleWatcherOnThread();

  SetTimeoutTaskOnThread(client_data);

  if (AddFrequencyOnThread(client_data->frequency))
    return;

  client_data->observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED);
}

void SamplesHandler::AddClientOnThread(
    ClientData* client_data,
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  if (inactive_clients_.find(client_data) != inactive_clients_.end() ||
      clients_map_.find(client_data) != clients_map_.end()) {
    LOGF(ERROR) << "Failed to AddClient: Already added";
    mojo::Remote<cros::mojom::SensorDeviceSamplesObserver>(std::move(observer))
        ->OnErrorOccurred(cros::mojom::ObserverErrorType::ALREADY_STARTED);
    return;
  }

  DCHECK(!client_data->observer.is_bound());
  client_data->observer.Bind(std::move(observer));
  client_data->observer.set_disconnect_handler(
      base::BindOnce(&SamplesHandler::OnSamplesObserverDisconnect,
                     weak_factory_.GetWeakPtr(), client_data));

  SensorMetrics::GetInstance()->SendSensorObserverOpened();

  client_data->frequency = FixFrequency(client_data->frequency);

  if (client_data->IsActive()) {
    AddActiveClientOnThread(client_data);
    return;
  }

  // Adding an inactive client.
  inactive_clients_.emplace(client_data);

  if (client_data->frequency < libmems::kFrequencyEpsilon) {
    LOGF(ERROR) << "Added an inactive client: Invalid frequency.";
    client_data->observer->OnErrorOccurred(
        cros::mojom::ObserverErrorType::FREQUENCY_INVALID);
  }
  if (client_data->enabled_chn_indices.empty()) {
    LOGF(ERROR) << "Added an inactive client: No enabled channels.";
    client_data->observer->OnErrorOccurred(
        cros::mojom::ObserverErrorType::NO_ENABLED_CHANNELS);
  }
}

void SamplesHandler::OnSamplesObserverDisconnect(ClientData* client_data) {
  DCHECK(sample_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SamplesObserver disconnected. ReceiverId: "
              << client_data->id;
  RemoveClientOnThread(client_data);
}

void SamplesHandler::RemoveActiveClientOnThread(ClientData* client_data,
                                                double orig_freq) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);
  DCHECK_GE(orig_freq, libmems::kFrequencyEpsilon);
  DCHECK(clients_map_.find(client_data) != clients_map_.end());

  clients_map_.erase(client_data);
  if (clients_map_.empty())
    StopSampleWatcherOnThread();

  if (RemoveFrequencyOnThread(orig_freq))
    return;

  // Failed to set frequency
  if (client_data->observer.is_bound()) {
    client_data->observer->OnErrorOccurred(
        cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED);
  }
}

void SamplesHandler::RemoveClientOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  client_data->observer.reset();

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    inactive_clients_.erase(it);
    SensorMetrics::GetInstance()->SendSensorObserverClosed();
    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end()) {
    LOGF(ERROR) << "Failed to RemoveClient: Client not found";
    return;
  }

  SensorMetrics::GetInstance()->SendSensorObserverClosed();
  RemoveActiveClientOnThread(client_data, client_data->frequency);
}

double SamplesHandler::FixFrequency(double frequency) {
  if (frequency < libmems::kFrequencyEpsilon)
    return 0.0;

  if (frequency > dev_max_frequency_)
    return dev_max_frequency_;

  return frequency;
}

double SamplesHandler::FixFrequencyWithMin(double frequency) {
  if (frequency < libmems::kFrequencyEpsilon)
    return 0.0;

  if (frequency < dev_min_frequency_)
    return dev_min_frequency_;

  if (frequency > dev_max_frequency_)
    return dev_max_frequency_;

  return frequency;
}

void SamplesHandler::UpdateFrequencyOnThread(
    ClientData* client_data,
    double frequency,
    cros::mojom::SensorDevice::SetFrequencyCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  frequency = FixFrequency(frequency);

  double orig_freq = client_data->frequency;
  client_data->frequency = frequency;
  ipc_task_runner_->PostTask(FROM_HERE,
                             base::BindOnce(std::move(callback), frequency));

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    if (client_data->IsActive()) {
      // The client is now active.
      inactive_clients_.erase(it);
      AddActiveClientOnThread(client_data);
    }

    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end())
    return;

  if (!client_data->IsActive()) {
    // The client is now inactive
    RemoveActiveClientOnThread(client_data, orig_freq);
    inactive_clients_.emplace(client_data);

    return;
  }

  // The client remains active
  DCHECK(client_data->observer.is_bound());

  if (AddFrequencyOnThread(client_data->frequency) &&
      RemoveFrequencyOnThread(orig_freq))
    return;

  // Failed to set device frequency
  client_data->observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED);
}

bool SamplesHandler::AddFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  frequencies_.emplace(frequency);
  double max_freq = *frequencies_.rbegin();
  return UpdateRequestedFrequencyOnThread(max_freq);
}
bool SamplesHandler::RemoveFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  auto it = frequencies_.find(frequency);
  DCHECK(it != frequencies_.end());
  frequencies_.erase(it);
  auto r_it = frequencies_.rbegin();
  double max_freq = (r_it == frequencies_.rend()) ? 0.0 : *r_it;
  DCHECK_LE(max_freq, requested_frequency_);
  return UpdateRequestedFrequencyOnThread(max_freq);
}

bool SamplesHandler::UpdateRequestedFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  // We didn't limit clients' frequency to be greater than or equal to
  // |dev_min_frequency_|, but we need to do that when setting the real
  // frequency.
  frequency = FixFrequencyWithMin(frequency);

  if (frequency == requested_frequency_)
    return true;

  SensorMetrics::GetInstance()->SendSensorUsage(iio_device_->GetId(),
                                                frequency);

  requested_frequency_ = frequency;

  if (!iio_device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr,
                                         frequency)) {
    /*
     * The frequency attributes may not exist on some sensors (acpi-als).
     * Ignore the error when the sensor does not have FIFO.
     */
    if (iio_device_->HasFifo()) {
      LOGF(ERROR) << "Failed to set frequency";
      return false;
    }
  }

  // |sampling_frequency| returns by the EC is the current sensors ODR. It may
  // be higher than requested when the EC needs higher speed, or just different
  // if the EC is slow to set the new sensor ODR. Use requested |frequency| as
  // base for downsampling.
  dev_frequency_ = frequency;

  if (iio_device_->HasFifo()) {
    double ec_period = 0;
    if (dev_frequency_ > libmems::kFrequencyEpsilon)
      ec_period = 1.0 / dev_frequency_;

    if (!iio_device_->WriteDoubleAttribute(libmems::kHWFifoTimeoutAttr,
                                           ec_period)) {
      LOGF(ERROR) << "Failed to set fifo timeout";
      return false;
    }

    return true;
  }

  DCHECK(iio_device_->GetHrtimer());

  // |iio_device_| has a trigger that needs to be setup.
  if (!iio_device_->GetHrtimer()->WriteDoubleAttribute(
          libmems::kSamplingFrequencyAttr, frequency)) {
    LOGF(ERROR) << "Failed to set hrtimer's frequency";
    return false;
  }

  return true;
}

void SamplesHandler::UpdateChannelsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    cros::mojom::SensorDevice::SetChannelsEnabledCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  std::vector<int32_t> failed_indices;

  if (en) {
    for (int32_t chn_index : iio_chn_indices) {
      auto chn = iio_device_->GetChannel(chn_index);
      if (!chn || !chn->IsEnabled()) {
        LOGF(ERROR) << "Failed to enable chn with index: " << chn_index;
        failed_indices.push_back(chn_index);
        continue;
      }

      client_data->enabled_chn_indices.emplace(chn_index);
    }
  } else {
    for (int32_t chn_index : iio_chn_indices) {
      client_data->enabled_chn_indices.erase(chn_index);
      // remove cached chn's moving average
      clients_map_[client_data].chns.erase(chn_index);
    }
  }

  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), std::move(failed_indices)));

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    if (client_data->IsActive()) {
      // The client is now active.
      inactive_clients_.erase(it);
      AddActiveClientOnThread(client_data);
    }

    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end())
    return;

  if (client_data->IsActive()) {
    // The client remains active
    return;
  }

  RemoveActiveClientOnThread(client_data, client_data->frequency);
  inactive_clients_.emplace(client_data);
}

void SamplesHandler::GetChannelsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    cros::mojom::SensorDevice::GetChannelsEnabledCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  std::vector<bool> enabled;

  for (int32_t chn_index : iio_chn_indices) {
    enabled.push_back(client_data->enabled_chn_indices.find(chn_index) !=
                      client_data->enabled_chn_indices.end());
  }

  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(enabled)));
}

void SamplesHandler::SetTimeoutTaskOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  if (client_data->timeout == 0)
    return;

  sample_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::SampleTimeout, weak_factory_.GetWeakPtr(),
                     client_data, clients_map_[client_data].sample_index),
      base::TimeDelta::FromMilliseconds(client_data->timeout));
}
void SamplesHandler::SampleTimeout(ClientData* client_data,
                                   uint64_t sample_index) {
  if (!client_data->observer.is_bound())
    return;

  auto it = clients_map_.find(client_data);
  if (it == clients_map_.end() || it->second.sample_index != sample_index)
    return;

  client_data->observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::READ_TIMEOUT);
}

void SamplesHandler::OnSampleAvailableWithoutBlocking() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK(num_read_failed_logs_ == 0 || num_read_failed_logs_recovery_ == 0);

  auto sample = iio_device_->ReadSample();
  if (!sample) {
    AddReadFailedLog();
    for (auto& [client_data, _] : clients_map_) {
      client_data->observer->OnErrorOccurred(
          cros::mojom::ObserverErrorType::READ_FAILED);
    }

    return;
  }

  if (num_read_failed_logs_ == 0) {
    if (num_read_failed_logs_recovery_ > 0 &&
        ++num_read_failed_logs_recovery_ >= kNumReadFailedLogsRecovery) {
      LOGF(INFO) << "Resuming error logs";
      num_read_failed_logs_recovery_ = 0;
    }
  } else {
    --num_read_failed_logs_;
  }

  for (auto& [client_data, sample_data] : clients_map_) {
    DCHECK(client_data->IsActive());
    DCHECK(client_data->observer.is_bound());

    int step =
        std::max(1, static_cast<int>(dev_frequency_ / client_data->frequency));

    // Update moving averages for channels
    for (int32_t chn_index : client_data->enabled_chn_indices) {
      if (no_batch_chn_indices.find(chn_index) != no_batch_chn_indices.end())
        continue;

      if (sample->find(chn_index) == sample->end()) {
        LOGF(ERROR) << "Missing chn index: " << chn_index << " in sample";
        continue;
      }

      int size = samples_cnt_ - sample_data.sample_index + 1;
      if (sample_data.chns.find(chn_index) == sample_data.chns.end() &&
          size != 1) {
        // A new enabled channel: fill up previous sample points with the
        // current value
        sample_data.chns[chn_index] =
            sample.value()[chn_index] * (size * (size - 1) / 2);
      }

      sample_data.chns[chn_index] += sample.value()[chn_index] * size;
    }

    if (sample_data.sample_index + step - 1 <= samples_cnt_) {
      // Send a sample to the client
      int64_t size = samples_cnt_ - sample_data.sample_index + 1;
      DCHECK_GE(size, 1);
      int64_t denom = ((size + 1) * size / 2);

      libmems::IioDevice::IioSample client_sample;
      for (int32_t chn_index : client_data->enabled_chn_indices) {
        if (sample->find(chn_index) == sample->end()) {
          LOGF(ERROR) << "Missing chn: " << chn_index << " in sample";
          continue;
        }

        if (no_batch_chn_indices.find(chn_index) !=
            no_batch_chn_indices.end()) {
          // Use the current value directly
          client_sample[chn_index] = sample.value()[chn_index];
          continue;
        }

        if (sample_data.chns.find(chn_index) == sample_data.chns.end()) {
          LOGF(ERROR) << "Missed chn index: " << chn_index
                      << " in moving averages";
          continue;
        }

        client_sample[chn_index] = sample_data.chns[chn_index] / denom;
      }

      sample_data.sample_index = samples_cnt_ + 1;
      sample_data.chns.clear();

      client_data->observer->OnSampleUpdated(std::move(client_sample));
      SetTimeoutTaskOnThread(client_data);
    }
  }

  ++samples_cnt_;
}

void SamplesHandler::AddReadFailedLog() {
  if (num_read_failed_logs_recovery_ > 0) {
    if (++num_read_failed_logs_recovery_ >= kNumReadFailedLogsRecovery) {
      LOGF(INFO) << "Resuming error logs";
      num_read_failed_logs_recovery_ = 0;
    }

    return;
  }

  if (++num_read_failed_logs_ >= kNumReadFailedLogsBeforeGivingUp) {
    LOGF(ERROR) << "Too many read failed logs: Skipping logs until "
                << kNumReadFailedLogsRecovery << " reads are done";

    num_read_failed_logs_ = 0;
    num_read_failed_logs_recovery_ = 1;
    return;
  }

  LOGF(ERROR) << "Failed to read a sample";
}

}  // namespace iioservice
