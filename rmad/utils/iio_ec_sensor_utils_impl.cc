// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/iio_ec_sensor_utils_impl.h"

#include <numeric>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace {

constexpr int kMaxNumEntries = 1024;
constexpr char kIioDevicePathPrefix[] = "/sys/bus/iio/devices/iio:device";
constexpr char kIioDeviceEntryName[] = "name";
constexpr char kIioDeviceEntryLocation[] = "location";
constexpr char kIioDeviceEntryFrequency[] = "sampling_frequency";
constexpr char kIioDeviceEntryFrequencyAvailable[] =
    "sampling_frequency_available";
constexpr char kIioDeviceEntryScale[] = "scale";

constexpr char kIioServiceClientCmdPath[] = "/usr/sbin/iioservice_simpleclient";
constexpr char kIioParameterChannelsPrefix[] = "--channels=";
constexpr char kIioParameterFrequencyPrefix[] = "--frequency=";
constexpr char kIioParameterDeviceIdPrefix[] = "--device_id=";
constexpr char kIioParameterSamplesPrefix[] = "--samples=";

}  // namespace

namespace rmad {

IioEcSensorUtilsImpl::IioEcSensorUtilsImpl(const std::string& location,
                                           const std::string& name)
    : IioEcSensorUtils(location, name), initialized_(false) {
  Initialize();
}

bool IioEcSensorUtilsImpl::GetAvgData(const std::vector<std::string>& channels,
                                      int samples,
                                      std::vector<double>* avg_data,
                                      std::vector<double>* variance) {
  CHECK(avg_data);

  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  std::string parameter_channels = kIioParameterChannelsPrefix;
  for (auto channel : channels) {
    parameter_channels += channel + " ";
  }

  std::vector<std::string> argv{
      kIioServiceClientCmdPath, parameter_channels,
      kIioParameterFrequencyPrefix + base::NumberToString(frequency_),
      kIioParameterDeviceIdPrefix + base::NumberToString(id_),
      kIioParameterSamplesPrefix + base::NumberToString(samples)};
  std::string value;
  if (!base::GetAppOutputAndError(argv, &value)) {
    std::string whole_cmd;
    for (auto arg : argv) {
      whole_cmd += " " + arg;
    }
    LOG(ERROR) << location_ << ":" << name_ << ": Failed to get data by"
               << whole_cmd;
    return false;
  }

  std::vector<std::vector<double>> data(channels.size());
  for (int i = 0; i < channels.size(); i++) {
    re2::StringPiece str_piece(value);
    re2::RE2 reg(channels[i] + R"(: ([-+]?\d+))");
    std::string match;
    double raw_data;

    while (RE2::FindAndConsume(&str_piece, reg, &match)) {
      if (!base::StringToDouble(match, &raw_data)) {
        LOG(WARNING) << location_ << ":" << name_ << ": Failed to parse data ["
                     << match << "]";
        continue;
      }

      data.at(i).push_back(raw_data * scale_);
    }

    if (data.at(i).size() != samples) {
      LOG(ERROR) << location_ << ":" << name_ << ":" << channels[i]
                 << ": We received " << data.at(i).size() << " instead of "
                 << samples << " samples.";
      return false;
    }
  }

  avg_data->resize(channels.size());
  for (int i = 0; i < channels.size(); i++) {
    avg_data->at(i) =
        std::accumulate(data.at(i).begin(), data.at(i).end(), 0.0) / samples;
  }

  if (!variance) {
    return true;
  }

  variance->resize(channels.size(), 0.0);
  for (int i = 0; i < channels.size(); i++) {
    for (const double& value : data.at(i)) {
      double var = (value - avg_data->at(i)) * (value - avg_data->at(i));
      variance->at(i) += var;
    }
    variance->at(i) /= data.at(i).size();
  }

  return true;
}

bool IioEcSensorUtilsImpl::SetSysValues(const std::vector<std::string>& entries,
                                        const std::vector<int>& values) {
  CHECK(entries.size() == values.size());

  if (!initialized_) {
    LOG(ERROR) << location_ << ":" << name_ << " is not initialized.";
    return false;
  }

  for (int i = 0; i < entries.size(); i++) {
    auto entry = sysfs_path_.Append(entries[i]);
    auto bytes = base::NumberToString(values[i]);
    int byte_size = bytes.size();
    if (!base::PathExists(entry) ||
        byte_size != base::WriteFile(entry, bytes.c_str(), byte_size)) {
      LOG(ERROR) << "Failed to write sys value at " << entry << " to " << bytes;
      return false;
    }
  }

  return true;
}

void IioEcSensorUtilsImpl::Initialize() {
  for (int i = 0; i < kMaxNumEntries; i++) {
    base::FilePath sysfs_path(kIioDevicePathPrefix + base::NumberToString(i));
    if (!base::PathExists(sysfs_path)) {
      break;
    }

    base::FilePath entry_name = sysfs_path.Append(kIioDeviceEntryName);
    if (std::string buf;
        !base::PathExists(entry_name) ||
        !base::ReadFileToString(entry_name, &buf) ||
        name_ != base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING)) {
      continue;
    }

    base::FilePath entry_location = sysfs_path.Append(kIioDeviceEntryLocation);
    if (std::string buf;
        !base::PathExists(entry_location) ||
        !base::ReadFileToString(entry_location, &buf) ||
        location_ != base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING)) {
      continue;
    }

    base::FilePath entry_frequency =
        sysfs_path.Append(kIioDeviceEntryFrequency);
    if (std::string buf;
        !base::PathExists(entry_frequency) ||
        !base::ReadFileToString(entry_frequency, &buf) ||
        !base::StringToDouble(
            base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING), &frequency_)) {
      continue;
    }

    // This is a special case where the sensor has not been set. We should set
    // it according to one of its available sampling frequency. We will use the
    // slowest one for calibration.
    if (frequency_ == 0) {
      base::FilePath entry_frequency_available =
          sysfs_path.Append(kIioDeviceEntryFrequencyAvailable);
      if (std::string buf;
          !base::PathExists(entry_frequency_available) ||
          !base::ReadFileToString(entry_frequency_available, &buf)) {
        continue;
      } else {
        // The value from sysfs could be like "0.000000 13.000000 208.000000"
        // or "13.000000 208.000000", and we will choose the first non-zero
        // frequency.
        re2::StringPiece str_piece(buf);
        re2::RE2 reg(R"((\d+.\d+))");
        std::string match;
        while (RE2::FindAndConsume(&str_piece, reg, &match)) {
          if (base::StringToDouble(match, &frequency_) && frequency_ > 0) {
            break;
          }
        }
        if (frequency_ == 0) {
          continue;
        }
      }
    }

    base::FilePath entry_scale = sysfs_path.Append(kIioDeviceEntryScale);
    if (std::string buf;
        !base::PathExists(entry_scale) ||
        !base::ReadFileToString(entry_scale, &buf) ||
        !base::StringToDouble(
            base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING), &scale_)) {
      continue;
    }

    id_ = i;
    sysfs_path_ = sysfs_path;
    initialized_ = true;
    break;
  }
}

}  // namespace rmad
