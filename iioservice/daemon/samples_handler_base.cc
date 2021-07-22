// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/samples_handler_base.h"

#include <utility>

#include <libmems/common_types.h>

#include "iioservice/daemon/sensor_metrics.h"
#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr uint32_t kNumReadFailedLogsBeforeGivingUp = 100;
constexpr uint32_t kNumReadFailedLogsRecovery = 10000;

constexpr char kNoBatchChannels[][10] = {"timestamp", "count"};

}  // namespace

SamplesHandlerBase::SamplesHandlerBase(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

void SamplesHandlerBase::SetNoBatchChannels(
    std::vector<std::string> channel_ids) {
  for (size_t i = 0; i < channel_ids.size(); ++i) {
    for (size_t j = 0; j < base::size(kNoBatchChannels); ++j) {
      if (channel_ids[i] == kNoBatchChannels[j]) {
        no_batch_chn_indices_.emplace(i);
        break;
      }
    }
  }
}

void SamplesHandlerBase::OnSamplesObserverDisconnect(ClientData* client_data) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SamplesObserver disconnected. ReceiverId: "
              << client_data->id;
  RemoveClientOnThread(client_data);
}

void SamplesHandlerBase::AddClientOnThread(
    ClientData* client_data,
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

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
      base::BindOnce(&SamplesHandlerBase::OnSamplesObserverDisconnect,
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

void SamplesHandlerBase::AddActiveClientOnThread(ClientData* client_data) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(client_data->IsActive());
  DCHECK(client_data->observer.is_bound());
  DCHECK(inactive_clients_.find(client_data) == inactive_clients_.end());
  DCHECK(clients_map_.find(client_data) == clients_map_.end());

  clients_map_.emplace(client_data, SampleData{});
  clients_map_[client_data].sample_index = samples_cnt_;

  SetTimeoutTaskOnThread(client_data);

  if (AddFrequencyOnThread(client_data->frequency))
    return;

  client_data->observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED);
}

void SamplesHandlerBase::RemoveClientOnThread(ClientData* client_data) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

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

void SamplesHandlerBase::RemoveActiveClientOnThread(ClientData* client_data,
                                                    double orig_freq) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_GE(orig_freq, libmems::kFrequencyEpsilon);
  DCHECK(clients_map_.find(client_data) != clients_map_.end());

  clients_map_.erase(client_data);

  if (RemoveFrequencyOnThread(orig_freq))
    return;

  // Failed to set frequency
  if (client_data->observer.is_bound()) {
    client_data->observer->OnErrorOccurred(
        cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED);
  }
}

double SamplesHandlerBase::FixFrequency(double frequency) {
  return frequency;
}

double SamplesHandlerBase::GetRequestedFrequencyOnThread() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto r_it = frequencies_.rbegin();
  return (r_it == frequencies_.rend()) ? 0.0 : *r_it;
}

bool SamplesHandlerBase::AddFrequencyOnThread(double frequency) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  frequencies_.emplace(frequency);
  return UpdateRequestedFrequencyOnThread();
}

bool SamplesHandlerBase::RemoveFrequencyOnThread(double frequency) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto it = frequencies_.find(frequency);
  DCHECK(it != frequencies_.end());
  frequencies_.erase(it);
  return UpdateRequestedFrequencyOnThread();
}

void SamplesHandlerBase::SetTimeoutTaskOnThread(ClientData* client_data) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (client_data->timeout == 0)
    return;

  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandlerBase::SampleTimeout,
                     weak_factory_.GetWeakPtr(), client_data,
                     clients_map_[client_data].sample_index),
      base::TimeDelta::FromMilliseconds(client_data->timeout));
}

void SamplesHandlerBase::SampleTimeout(ClientData* client_data,
                                       uint64_t sample_index) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!client_data->observer.is_bound())
    return;

  auto it = clients_map_.find(client_data);
  if (it == clients_map_.end() || it->second.sample_index != sample_index)
    return;

  client_data->observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::READ_TIMEOUT);
}

void SamplesHandlerBase::OnSampleAvailableOnThread(
    const base::flat_map<int32_t, int64_t>& sample) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (num_read_failed_logs_ == 0) {
    if (num_read_failed_logs_recovery_ > 0 &&
        ++num_read_failed_logs_recovery_ >= kNumReadFailedLogsRecovery) {
      LOGF(INFO) << "Resuming error logs";
      num_read_failed_logs_recovery_ = 0;
    }
  } else {
    --num_read_failed_logs_;
  }

  double requested_frequency =
      dev_frequency_ > 0 ? dev_frequency_ : requested_frequency_;
  for (auto& [client_data, sample_data] : clients_map_) {
    DCHECK(client_data->IsActive());
    DCHECK(client_data->observer.is_bound());

    int step = std::max(
        1, static_cast<int>(requested_frequency / client_data->frequency));

    // Update moving averages for channels
    for (int32_t chn_index : client_data->enabled_chn_indices) {
      if (no_batch_chn_indices_.find(chn_index) != no_batch_chn_indices_.end())
        continue;

      auto it = sample.find(chn_index);
      if (it == sample.end()) {
        LOGF(ERROR) << "Missing chn index: " << chn_index << " in sample";
        continue;
      }

      int size = samples_cnt_ - sample_data.sample_index + 1;
      if (sample_data.chns.find(chn_index) == sample_data.chns.end() &&
          size != 1) {
        // A new enabled channel: fill up previous sample points with the
        // current value
        sample_data.chns[chn_index] = it->second * (size * (size - 1) / 2);
      }

      sample_data.chns[chn_index] += it->second * size;
    }

    if (sample_data.sample_index + step - 1 <= samples_cnt_) {
      // Send a sample to the client
      int64_t size = samples_cnt_ - sample_data.sample_index + 1;
      DCHECK_GE(size, 1);
      int64_t denom = ((size + 1) * size / 2);

      libmems::IioDevice::IioSample client_sample;
      for (int32_t chn_index : client_data->enabled_chn_indices) {
        auto it = sample.find(chn_index);
        if (it == sample.end()) {
          LOGF(ERROR) << "Missing chn: " << chn_index << " in sample";
          continue;
        }

        if (no_batch_chn_indices_.find(chn_index) !=
            no_batch_chn_indices_.end()) {
          // Use the current value directly
          client_sample[chn_index] = it->second;
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

void SamplesHandlerBase::AddReadFailedLogOnThread() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

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
