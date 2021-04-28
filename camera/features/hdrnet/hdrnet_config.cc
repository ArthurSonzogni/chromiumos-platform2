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

constexpr char kEnableKey[] = "enable";
constexpr char kExpCompKey[] = "exp_comp";
constexpr char kFaceDetectionEnableKey[] = "face_detection_enable";
constexpr char kGcamAeEnableKey[] = "gcam_ae_enable";
constexpr char kGcamAeIntervalKey[] = "gcam_ae_interval";
constexpr char kHdrRatioKey[] = "hdr_ratio";

}  // namespace

HdrNetConfig::HdrNetConfig(const char* config_file_path)
    : config_file_path_(config_file_path) {
  bool ret = file_path_watcher_.Watch(
      config_file_path_, base::FilePathWatcher::Type::kNonRecursive,
      base::BindRepeating(&HdrNetConfig::OnConfigFileUpdated,
                          base::Unretained(this)));
  if (!ret) {
    LOGF(ERROR) << "Can't monitor HDRnet config file path: "
                << config_file_path_;
    return;
  }
  ReadConfigFile();
}

HdrNetConfig::Options HdrNetConfig::GetOptions() {
  base::AutoLock l(options_lock_);
  return options_;
}

bool HdrNetConfig::ReadConfigFile() {
  if (!base::PathExists(config_file_path_)) {
    return false;
  }

  constexpr size_t kConfigFileMaxSize = 1024;
  std::string contents;
  CHECK(base::ReadFileToStringWithMaxSize(config_file_path_, &contents,
                                          kConfigFileMaxSize));
  base::Optional<base::Value> json_values =
      base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!json_values) {
    LOGF(ERROR) << "Failed to load the content of HDRnet config file";
    return false;
  }

  base::AutoLock l(options_lock_);
  auto enable = json_values->FindBoolKey(kEnableKey);
  if (enable) {
    options_.enable = *enable;
  }
  auto hdr_ratio = json_values->FindDoubleKey(kHdrRatioKey);
  if (hdr_ratio) {
    options_.hdr_ratio = *hdr_ratio;
  }
  auto gcam_ae_enable = json_values->FindBoolKey(kGcamAeEnableKey);
  if (gcam_ae_enable) {
    options_.gcam_ae_enable = *gcam_ae_enable;
  }
  auto gcam_ae_interval = json_values->FindIntKey(kGcamAeIntervalKey);
  if (gcam_ae_interval) {
    options_.gcam_ae_interval = *gcam_ae_interval;
  }
  auto face_detection_enable =
      json_values->FindBoolKey(kFaceDetectionEnableKey);
  if (face_detection_enable) {
    options_.face_detection_enable = *face_detection_enable;
  }
  auto exp_comp = json_values->FindIntKey(kExpCompKey);
  if (exp_comp) {
    options_.exp_comp = *exp_comp;
  }

  VLOGF(1) << "HDRnet config:"
           << " enable=" << options_.enable
           << " hdr_ratio=" << options_.hdr_ratio
           << " gcam_ae_enable=" << options_.gcam_ae_enable
           << " gcam_ae_interval=" << options_.gcam_ae_interval
           << " face_detection_enable=" << options_.face_detection_enable
           << " exp_comp=" << options_.exp_comp;

  return true;
}

void HdrNetConfig::OnConfigFileUpdated(const base::FilePath& file_path,
                                       bool error) {
  ReadConfigFile();
}

}  // namespace cros
