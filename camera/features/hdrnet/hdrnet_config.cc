/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_config.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <system/camera_metadata.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kDumpBufferKey[] = "dump_buffer";
constexpr char kHdrNetEnableKey[] = "hdrnet_enable";
constexpr char kHdrRatioKey[] = "hdr_ratio";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";

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
  if (base::PathExists(override_config_file_path_)) {
    ReadConfigFile(override_config_file_path_);
  }
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
  auto hdr_ratio = json_values->FindDoubleKey(kHdrRatioKey);
  if (hdr_ratio) {
    options_.hdr_ratio = *hdr_ratio;
  }
  auto dump_buffer = json_values->FindBoolKey(kDumpBufferKey);
  if (dump_buffer) {
    options_.dump_buffer = *dump_buffer;
  }
  auto log_frame_metadata = json_values->FindBoolKey(kLogFrameMetadataKey);
  if (log_frame_metadata) {
    options_.log_frame_metadata = *log_frame_metadata;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "HDRnet config:"
             << " hdrnet_enable=" << options_.hdrnet_enable
             << " hdr_ratio=" << options_.hdr_ratio
             << " dump_buffer=" << options_.dump_buffer
             << " log_frame_metadata=" << options_.log_frame_metadata;
  }

  return true;
}

void HdrNetConfig::OnConfigFileUpdated(const base::FilePath& file_path,
                                       bool error) {
  ReadConfigFile(override_config_file_path_);
}

}  // namespace cros
