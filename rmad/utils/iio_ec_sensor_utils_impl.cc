// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/iio_ec_sensor_utils_impl.h"

#include <numeric>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <libmems/iio_context_impl.h>

namespace {

constexpr int kTimeoutOverheadInMS = 1000;
constexpr double kSecond2Millisecond = 1000.0;
constexpr int kNumberFirstReadsDiscarded = 10;

constexpr char kIioDevicePathPrefix[] = "/sys/bus/iio/devices/iio:device";
constexpr char kIioDeviceEntryScale[] = "scale";

}  // namespace

namespace rmad {

IioEcSensorUtilsImpl::IioEcSensorUtilsImpl(
    scoped_refptr<MojoServiceUtils> mojo_service,
    const std::string& location,
    const std::string& name)
    : IioEcSensorUtils(location, name),
      sysfs_prefix_(kIioDevicePathPrefix),
      initialized_(false),
      mojo_service_(mojo_service),
      iio_context_(std::make_unique<libmems::IioContextImpl>()) {
  Initialize();
}

IioEcSensorUtilsImpl::IioEcSensorUtilsImpl(
    scoped_refptr<MojoServiceUtils> mojo_service,
    const std::string& location,
    const std::string& name,
    const std::string& sysfs_prefix,
    std::unique_ptr<libmems::IioContext> iio_context)
    : IioEcSensorUtils(location, name),
      sysfs_prefix_(sysfs_prefix),
      initialized_(false),
      mojo_service_(mojo_service),
      iio_context_(std::move(iio_context)) {
  Initialize();
}

bool IioEcSensorUtilsImpl::GetAvgData(GetAvgDataCallback result_callback,
                                      const std::vector<std::string>& channels,
                                      int samples) {
  CHECK_GT(channels.size(), 0);
  CHECK_GT(samples, 0);

  // Bind callback for OnSampleUpdated.
  get_avg_data_result_callback_ = std::move(result_callback);

  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  target_channels_ = channels;
  sample_times_ = samples;
  samples_to_discard_ = kNumberFirstReadsDiscarded;
  sampled_data_.clear();

  mojo_service_->GetSensorDevice(id_)->GetAllChannelIds(
      base::BindOnce(&IioEcSensorUtilsImpl::HandleGetAllChannelIds,
                     weak_ptr_factory_.GetMutableWeakPtr()));

  return true;
}

bool IioEcSensorUtilsImpl::GetSysValues(const std::vector<std::string>& entries,
                                        std::vector<double>* values) const {
  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  std::vector<double> buffer_values;
  for (int i = 0; i < entries.size(); i++) {
    auto entry = sysfs_path_.Append(entries[i]);
    double val = 0.0;
    if (std::string str_val;
        !base::PathExists(entry) || !base::ReadFileToString(entry, &str_val) ||
        !base::StringToDouble(
            base::TrimWhitespaceASCII(str_val, base::TRIM_ALL), &val)) {
      LOG(ERROR) << "Failed to read sys value at " << entry;
      return false;
    }
    buffer_values.push_back(val);
  }

  *values = buffer_values;
  return true;
}

void IioEcSensorUtilsImpl::Initialize() {
  for (const auto& device : iio_context_->GetAllDevices()) {
    auto device_location = device->GetLocation();
    auto device_name = device->GetName();

    if (!device_location.has_value() || location_ != device_location.value() ||
        name_ != device_name) {
      continue;
    }
    id_ = device->GetId();

    double unused_min_freq, max_freq;
    if (!device->GetMinMaxFrequency(&unused_min_freq, &max_freq)) {
      LOG(ERROR) << "Failed to get frequencies of " << location_ << ":"
                 << name_;
      return;
    }
    frequency_ = max_freq;

    auto scale = device->ReadDoubleAttribute(kIioDeviceEntryScale);
    if (!scale.has_value()) {
      LOG(ERROR) << "Failed to get scale of " << location_ << ":" << name_;
      return;
    }
    scale_ = scale.value();

    sysfs_path_ = base::FilePath(sysfs_prefix_ + base::NumberToString(id_));

    initialized_ = true;
    break;
  }

  if (!initialized_) {
    LOG(ERROR) << "Failed to initialize " << location_ << ":" << name_;
  }
}

void IioEcSensorUtilsImpl::OnSampleUpdated(
    const base::flat_map<int, int64_t>& data) {
  // TODO(jeffulin): Remove this workaround when new firmware is released.
  if (samples_to_discard_-- > 0)
    return;

  for (const auto& channel_id : target_channel_ids_) {
    sampled_data_[channel_id].push_back(
        static_cast<double>(data.at(channel_id)) * scale_);
  }

  // Check if we got enough samples and stop sampling.
  if (sampled_data_.at(target_channel_ids_.at(0)).size() == sample_times_) {
    mojo_service_->GetSensorDevice(id_)->StopReadingSamples();
    DLOG(INFO) << "Stopped sampling";
    FinishSampling();
  }
}

void IioEcSensorUtilsImpl::FinishSampling() {
  std::vector<double> avg_data;
  avg_data.resize(target_channels_.size());
  for (int i = 0; i < target_channels_.size(); i++) {
    const int channel_id = target_channel_ids_.at(i);
    avg_data.at(i) = std::accumulate(sampled_data_.at(channel_id).begin(),
                                     sampled_data_.at(channel_id).end(), 0.0) /
                     sample_times_;
  }

  std::vector<double> variance;
  variance.resize(target_channels_.size(), 0.0);
  for (int i = 0; i < target_channels_.size(); i++) {
    const double avg = avg_data.at(i);
    const int channel_id = target_channel_ids_.at(i);
    for (const double value : sampled_data_.at(channel_id)) {
      variance.at(i) += (value - avg) * (value - avg);
    }
    variance.at(i) /=
        static_cast<double>(sampled_data_.at(channel_id).size() - 1);
  }

  std::move(get_avg_data_result_callback_)
      .Run(std::move(avg_data), std::move(variance));
}

void IioEcSensorUtilsImpl::OnErrorOccurred(
    cros::mojom::ObserverErrorType type) {
  LOG(ERROR) << "Got observer error while reading samples: " << type;
  std::move(get_avg_data_result_callback_).Run({}, {});
}

void IioEcSensorUtilsImpl::HandleSetChannelsEnabled(
    const std::vector<int>& failed_channel_ids) {
  if (!failed_channel_ids.empty()) {
    LOG(ERROR) << "Failed to enable channels.";
    std::move(get_avg_data_result_callback_).Run({}, {});
    return;
  }

  // Prepare for reading samples.
  device_sample_receiver_.reset();
  mojo_service_->GetSensorDevice(id_)->StartReadingSamples(
      device_sample_receiver_.BindNewPipeAndPassRemote());
}

void IioEcSensorUtilsImpl::HandleGetAllChannelIds(
    const std::vector<std::string>& channels) {
  channel_id_map_.clear();
  target_channel_ids_.clear();

  for (auto it = channels.begin(); it != channels.end(); it++) {
    channel_id_map_[*it] = it - channels.begin();
  }

  for (const auto& channel : target_channels_) {
    if (channel_id_map_.find(channel) == channel_id_map_.end()) {
      LOG(ERROR) << "Channel \"" << channel
                 << "\" is not an available channel.";
      std::move(get_avg_data_result_callback_).Run({}, {});
      return;
    }
    target_channel_ids_.push_back(channel_id_map_[channel]);
  }

  mojo_service_->GetSensorDevice(id_)->SetTimeout(
      ceil(kSecond2Millisecond / frequency_) + kTimeoutOverheadInMS);
  mojo_service_->GetSensorDevice(id_)->SetFrequency(frequency_,
                                                    base::DoNothing());

  mojo_service_->GetSensorDevice(id_)->SetChannelsEnabled(
      target_channel_ids_, true,
      base::BindOnce(&IioEcSensorUtilsImpl::HandleSetChannelsEnabled,
                     weak_ptr_factory_.GetMutableWeakPtr()));
}

}  // namespace rmad
