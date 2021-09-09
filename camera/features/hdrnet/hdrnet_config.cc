/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_config.h"

#include <string>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <system/camera_metadata.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kAeFrameIntervalKey[] = "ae_frame_interval";
constexpr char kAeOverrideModeKey[] = "ae_override_mode";
constexpr char kAeStatsInputModeKey[] = "ae_stats_input_mode";
constexpr char kDumpBufferKey[] = "dump_buffer";
constexpr char kExposureCompensationKey[] = "exp_comp";
constexpr char kFaceDetectionEnableKey[] = "face_detection_enable";
constexpr char kFdFrameIntervalKey[] = "fd_frame_interval";
constexpr char kGcamAeEnableKey[] = "gcam_ae_enable";
constexpr char kHdrNetEnableKey[] = "hdrnet_enable";
constexpr char kHdrRatioKey[] = "hdr_ratio";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";
constexpr char kMaxHdrRatio[] = "max_hdr_ratio";

}  // namespace

HdrNetConfig::HdrNetConfig(const char* default_config_file_path,
                           const char* override_config_file_path)
    : default_config_file_path_(default_config_file_path),
      override_config_file_path_(override_config_file_path) {
  bool ret = override_file_path_watcher_.Watch(
      override_config_file_path_, base::FilePathWatcher::Type::kNonRecursive,
      base::BindRepeating(&HdrNetConfig::OnConfigFileUpdated,
                          base::Unretained(this)));
  if (!ret) {
    LOGF(ERROR) << "Can't monitor HDRnet config file path: "
                << override_config_file_path_;
    return;
  }
  ReadConfigFile(default_config_file_path_);
}

HdrNetConfig::Options HdrNetConfig::GetOptions() {
  base::AutoLock l(options_lock_);
  return options_;
}

bool HdrNetConfig::ReadConfigFile(const base::FilePath& file_path) {
  if (!base::PathExists(file_path)) {
    return false;
  }

  constexpr size_t kConfigFileMaxSize = 1024;
  std::string contents;
  CHECK(base::ReadFileToStringWithMaxSize(file_path, &contents,
                                          kConfigFileMaxSize));
  base::Optional<base::Value> json_values =
      base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!json_values) {
    LOGF(ERROR) << "Failed to load the content of HDRnet config file";
    return false;
  }

  base::AutoLock l(options_lock_);
  auto hdrnet_enable = json_values->FindBoolKey(kHdrNetEnableKey);
  if (hdrnet_enable) {
    options_.hdrnet_enable = *hdrnet_enable;
  }
  auto gcam_ae_enable = json_values->FindBoolKey(kGcamAeEnableKey);
  if (gcam_ae_enable) {
    options_.gcam_ae_enable = *gcam_ae_enable;
  }
  auto ae_frame_interval = json_values->FindIntKey(kAeFrameIntervalKey);
  if (ae_frame_interval) {
    options_.ae_frame_interval = *ae_frame_interval;
  }
  auto max_hdr_ratio = json_values->FindDoubleKey(kMaxHdrRatio);
  if (max_hdr_ratio) {
    options_.max_hdr_ratio = *max_hdr_ratio;
  }
  auto ae_stats_input_mode = json_values->FindIntKey(kAeStatsInputModeKey);
  if (ae_stats_input_mode) {
    if (*ae_stats_input_mode ==
            static_cast<int>(AeStatsInputMode::kFromVendorAeStats) ||
        *ae_stats_input_mode ==
            static_cast<int>(AeStatsInputMode::kFromYuvImage)) {
      options_.ae_stats_input_mode =
          static_cast<AeStatsInputMode>(*ae_stats_input_mode);
    } else {
      LOGF(ERROR) << "Invalid AE stats input mode: " << *ae_stats_input_mode;
    }
  }
  auto ae_override_method = json_values->FindIntKey(kAeOverrideModeKey);
  if (ae_override_method) {
    if (*ae_override_method ==
            static_cast<int>(AeOverrideMode::kWithExposureCompensation) ||
        *ae_override_method ==
            static_cast<int>(AeOverrideMode::kWithManualSensorControl)) {
      options_.ae_override_mode =
          static_cast<AeOverrideMode>(*ae_override_method);
    } else {
      LOGF(ERROR) << "Invalid AE override method: " << *ae_override_method;
    }
  }
  auto use_cros_face_detector =
      json_values->FindBoolKey(kFaceDetectionEnableKey);
  if (use_cros_face_detector) {
    options_.use_cros_face_detector = *use_cros_face_detector;
  }
  auto fd_frame_interval = json_values->FindIntKey(kFdFrameIntervalKey);
  if (fd_frame_interval) {
    options_.fd_frame_interval = *fd_frame_interval;
  }
  auto hdr_ratio = json_values->FindDoubleKey(kHdrRatioKey);
  if (hdr_ratio) {
    options_.hdr_ratio = *hdr_ratio;
  }
  auto exp_comp = json_values->FindDoubleKey(kExposureCompensationKey);
  if (exp_comp) {
    options_.exposure_compensation = *exp_comp;
  }
  auto dump_buffer = json_values->FindBoolKey(kDumpBufferKey);
  if (dump_buffer) {
    options_.dump_buffer = *dump_buffer;
  }
  auto log_frame_metadata = json_values->FindBoolKey(kLogFrameMetadataKey);
  if (log_frame_metadata) {
    options_.log_frame_metadata = *log_frame_metadata;
  }

  VLOGF(1) << "HDRnet config:"
           << " hdrnet_enable=" << options_.hdrnet_enable
           << " hdr_ratio=" << options_.hdr_ratio
           << " gcam_ae_enable=" << options_.gcam_ae_enable
           << " ae_frame_interval=" << options_.ae_frame_interval
           << " max_hdr_ratio=" << options_.max_hdr_ratio
           << " ae_stats_input_mode="
           << static_cast<int>(options_.ae_stats_input_mode)
           << " use_cros_face_detector=" << options_.use_cros_face_detector
           << " fd_frame_interval=" << options_.fd_frame_interval
           << " exposure_compensation=" << options_.exposure_compensation
           << " dump_buffer=" << options_.dump_buffer
           << " log_frame_metadata=" << options_.log_frame_metadata;

  return true;
}

void HdrNetConfig::OnConfigFileUpdated(const base::FilePath& file_path,
                                       bool error) {
  ReadConfigFile(override_config_file_path_);
}

}  // namespace cros
