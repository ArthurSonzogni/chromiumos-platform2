// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/samples_observer.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/time/time.h>
#include <libmems/common_types.h>

#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr int kSetUpChannelTimeoutInMilliseconds = 3000;

// Set the base latency tolerance to half of 100 ms, according to
// https://source.android.com/compatibility/android-cdd#7_3_sensors, as the
// samples may go through a VM and Android sensormanager.
constexpr base::TimeDelta kMaximumBaseLatencyTolerance = base::Milliseconds(50);

}  // namespace

// static
SamplesObserver::ScopedSamplesObserver SamplesObserver::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    int device_id,
    cros::mojom::DeviceType device_type,
    std::vector<std::string> channel_ids,
    double frequency,
    int timeout,
    int samples,
    QuitCallback quit_callback) {
  ScopedSamplesObserver observer(
      new SamplesObserver(ipc_task_runner, device_id, device_type,
                          std::move(channel_ids), frequency, timeout, samples,
                          std::move(quit_callback)),
      SensorClientDeleter);

  return observer;
}

void SamplesObserver::OnSampleUpdated(
    const base::flat_map<int32_t, int64_t>& sample) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_GT(result_freq_, 0.0);

  if (sample.size() != channel_indices_.size()) {
    LOGF(ERROR) << "Invalid sample size: " << sample.size()
                << ", expected size: " << channel_indices_.size();
  }

  for (auto chn : sample)
    LOGF(INFO) << iio_chn_ids_[chn.first] << ": " << chn.second;

  if (timestamp_index_.has_value()) {
    auto it = sample.find(timestamp_index_.value());
    if (it != sample.end()) {
      struct timespec ts = {};
      if (clock_gettime(CLOCK_BOOTTIME, &ts) < 0) {
        PLOG(ERROR) << "clock_gettime(CLOCK_BOOTTIME) failed";
      } else {
        auto latency = base::Nanoseconds(static_cast<int64_t>(ts.tv_sec) *
                                             1000 * 1000 * 1000 +
                                         ts.tv_nsec - it->second);
        LOGF(INFO) << "Latency: " << latency;
        total_latency_ += latency;
        latencies_.push_back(latency);
      }
    }
  }

  if (++num_success_reads_ < samples_)
    return;

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(INFO) << "Number of success reads " << samples_ << " achieved";

  // Calculate the latencies only when timestamp channel is enabled.
  if (!latencies_.empty()) {
    base::TimeDelta latency_tolerance =
        kMaximumBaseLatencyTolerance + base::Seconds(1.0 / result_freq_);

    size_t n = latencies_.size();
    std::nth_element(latencies_.begin(), latencies_.begin(), latencies_.end());
    base::TimeDelta min_latency = latencies_[0];

    std::nth_element(latencies_.begin(), latencies_.begin() + n / 2,
                     latencies_.end());
    base::TimeDelta median_latency = latencies_[n / 2];

    std::nth_element(latencies_.begin(), --latencies_.end(), latencies_.end());
    base::TimeDelta max_latency = *(--latencies_.end());

    if (max_latency > latency_tolerance)
      // Don't Change: Used as a check sentence in the tast test.
      LOGF(ERROR) << "Max latency exceeds latency tolerance.";

    if (max_latency > latency_tolerance) {
      // Don't Change: Used as a check sentence in the tast test.
      LOG(ERROR) << "Max Latency exceeds Latency Tolerance.";
      LOG(ERROR) << "Latency Tolerance: " << latency_tolerance;
      LOG(ERROR) << "Max latency      : " << max_latency;
    } else {
      LOG(INFO) << "Latency tolerance: " << latency_tolerance;
      LOG(INFO) << "Max latency      : " << max_latency;
    }

    if (min_latency < base::Seconds(0.0)) {
      // Don't Change: Used as a check sentence in the tast test.
      LOGF(ERROR)
          << "Min latency less than zero: a timestamp was set in the past.";
      LOG(ERROR) << "Min latency      : " << min_latency;
    } else {
      LOG(INFO) << "Min latency      : " << min_latency;
    }

    LOG(INFO) << "Median latency   : " << median_latency;
    LOG(INFO) << "Mean latency     : " << total_latency_ / n;
  }

  Reset();
}

void SamplesObserver::OnErrorOccurred(cros::mojom::ObserverErrorType type) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(ERROR) << "OnErrorOccurred: " << type;
  Reset();
}

SamplesObserver::SamplesObserver(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    int device_id,
    cros::mojom::DeviceType device_type,
    std::vector<std::string> channel_ids,
    double frequency,
    int timeout,
    int samples,
    QuitCallback quit_callback)
    : SensorClient(std::move(ipc_task_runner), std::move(quit_callback)),
      device_id_(device_id),
      device_type_(device_type),
      channel_ids_(std::move(channel_ids)),
      frequency_(frequency),
      timeout_(timeout),
      samples_(samples),
      receiver_(this) {
  ipc_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SamplesObserver::SetUpChannelTimeout,
                     weak_factory_.GetWeakPtr()),
      base::Milliseconds(kSetUpChannelTimeoutInMilliseconds));
}

void SamplesObserver::Start() {
  if (device_id_ < 0)
    GetDeviceIdsByType();
  else
    GetSensorDevice();
}

mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver>
SamplesObserver::GetRemote() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto remote = receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(base::BindOnce(
      &SamplesObserver::OnObserverDisconnect, weak_factory_.GetWeakPtr()));

  return remote;
}

void SamplesObserver::Reset() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_device_remote_.reset();
  receiver_.reset();

  SensorClient::Reset();
}

void SamplesObserver::OnDeviceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorDevice disconnected";
  Reset();
}

void SamplesObserver::OnObserverDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "Observer diconnected";
  Reset();
}

void SamplesObserver::GetDeviceIdsByType() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_NE(device_type_, cros::mojom::DeviceType::NONE);

  sensor_service_remote_->GetDeviceIds(
      device_type_, base::BindOnce(&SamplesObserver::GetDeviceIdsCallback,
                                   weak_factory_.GetWeakPtr()));
}

void SamplesObserver::GetDeviceIdsCallback(
    const std::vector<int32_t>& iio_device_ids) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (iio_device_ids.empty()) {
    LOGF(ERROR) << "No device found give device type: " << device_type_;
    Reset();
  }

  // Take the first id.
  device_id_ = iio_device_ids.front();
  GetSensorDevice();
}

void SamplesObserver::GetSensorDevice() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (!sensor_device_remote_.is_bound()) {
    sensor_service_remote_->GetDevice(
        device_id_, sensor_device_remote_.BindNewPipeAndPassReceiver());

    sensor_device_remote_.set_disconnect_handler(base::BindOnce(
        &SamplesObserver::OnDeviceDisconnect, weak_factory_.GetWeakPtr()));
  }

  GetAllChannelIds();
}

void SamplesObserver::GetAllChannelIds() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_device_remote_->GetAllChannelIds(base::BindOnce(
      &SamplesObserver::GetAllChannelIdsCallback, weak_factory_.GetWeakPtr()));
}

void SamplesObserver::GetAllChannelIdsCallback(
    const std::vector<std::string>& iio_chn_ids) {
  iio_chn_ids_ = std::move(iio_chn_ids);
  channel_indices_.clear();

  for (int32_t i = 0; i < channel_ids_.size(); ++i) {
    for (int32_t j = 0; j < iio_chn_ids_.size(); ++j) {
      if (channel_ids_[i] == iio_chn_ids_[j]) {
        channel_indices_.push_back(j);
        break;
      }
    }
  }

  for (int32_t j = 0; j < iio_chn_ids_.size(); ++j) {
    if (iio_chn_ids_[j].compare(libmems::kTimestampAttr) == 0) {
      timestamp_index_ = j;
      break;
    }
  }

  if (channel_indices_.empty()) {
    LOGF(ERROR) << "No available channels";
    Reset();

    return;
  }

  StartReading();
}

void SamplesObserver::StartReading() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_device_remote_->SetTimeout(timeout_);
  sensor_device_remote_->SetFrequency(
      frequency_, base::BindOnce(&SamplesObserver::SetFrequencyCallback,
                                 weak_factory_.GetWeakPtr()));
  sensor_device_remote_->SetChannelsEnabled(
      channel_indices_, true,
      base::BindOnce(&SamplesObserver::SetChannelsEnabledCallback,
                     weak_factory_.GetWeakPtr()));

  sensor_device_remote_->StartReadingSamples(GetRemote());
}

void SamplesObserver::SetFrequencyCallback(double result_freq) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  result_freq_ = result_freq;
  if (result_freq_ > 0.0)
    return;

  LOGF(ERROR) << "Failed to set frequency";
  Reset();
}

void SamplesObserver::SetChannelsEnabledCallback(
    const std::vector<int32_t>& failed_indices) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  for (int32_t index : failed_indices) {
    LOGF(ERROR) << "Failed channel index: " << index;
    bool found = false;
    for (auto it = channel_indices_.begin(); it != channel_indices_.end();
         ++it) {
      if (index == *it) {
        found = true;
        channel_indices_.erase(it);
        break;
      }
    }

    if (!found)
      LOGF(ERROR) << index << " not in requested indices";
  }

  if (channel_indices_.empty()) {
    LOGF(ERROR) << "No channel enabled";
    Reset();
  }
}

}  // namespace iioservice
