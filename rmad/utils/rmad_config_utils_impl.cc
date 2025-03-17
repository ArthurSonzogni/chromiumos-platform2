// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/rmad_config_utils_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <google/protobuf/text_format.h>

#include "rmad/constants.h"
#include "rmad/utils/cros_config_utils_impl.h"

namespace rmad {

RmadConfigUtilsImpl::RmadConfigUtilsImpl()
    : config_dir_path_(kDefaultConfigDirPath) {
  cros_config_utils_ = std::make_unique<CrosConfigUtilsImpl>();
  Initialize();
}

RmadConfigUtilsImpl::RmadConfigUtilsImpl(
    const base::FilePath& config_dir_path,
    std::unique_ptr<CrosConfigUtils> cros_config_utils)
    : config_dir_path_(config_dir_path),
      cros_config_utils_(std::move(cros_config_utils)) {
  Initialize();
}

void RmadConfigUtilsImpl::Initialize() {
  std::string model_name;
  if (!cros_config_utils_->GetModelName(&model_name)) {
    LOG(ERROR) << "Failed to get model name";
    return;
  }

  const base::FilePath textproto_file_path =
      config_dir_path_.Append(model_name)
          .Append(kDefaultRmadConfigProtoFilePath);
  if (!base::PathExists(textproto_file_path)) {
    return;
  }

  std::string textproto;
  if (!base::ReadFileToString(textproto_file_path, &textproto)) {
    LOG(ERROR) << "Failed to read " << textproto_file_path.value();
    return;
  }

  RmadConfig rmad_config;
  if (!google::protobuf::TextFormat::ParseFromString(textproto, &rmad_config)) {
    LOG(ERROR) << "Failed to parse RmadConfig";
    return;
  }

  rmad_config_ = std::move(rmad_config);
}

const std::optional<RmadConfig>& RmadConfigUtilsImpl::GetConfig() const {
  return rmad_config_;
}

}  // namespace rmad
