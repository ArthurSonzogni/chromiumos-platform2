// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <libcrossystem/crossystem.h>

#include "runtime_probe/probe_config_loader.h"
#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace {

std::string HashProbeConfigSHA1(const std::string& content) {
  const auto& hash_val = base::SHA1HashString(content);
  return base::HexEncode(hash_val.data(), hash_val.size());
}

}  // namespace

std::optional<ProbeConfigData> LoadProbeConfigDataFromFile(
    const base::FilePath& file_path) {
  DVLOG(3) << "LoadProbeConfigDataFromFile: " << file_path;
  std::string config_json;
  if (!base::ReadFileToString(file_path, &config_json)) {
    return std::nullopt;
  }
  auto json_val = base::JSONReader::Read(config_json, base::JSON_PARSE_RFC);
  if (!json_val || !json_val->is_dict()) {
    DVLOG(3) << "Failed to parse probe config as JSON.";
    return std::nullopt;
  }

  const auto probe_config_sha1_hash = HashProbeConfigSHA1(config_json);
  DVLOG(3) << "SHA1 hash of probe config: " << probe_config_sha1_hash;

  const auto absolute_path = base::MakeAbsoluteFilePath(file_path);
  return ProbeConfigData{.path = absolute_path,
                         .config = std::move(*json_val),
                         .sha1_hash = std::move(probe_config_sha1_hash)};
}

CrosDebugFlag CrosDebug() {
  auto value = Context::Get()->crossystem()->VbGetSystemPropertyInt(
      kCrosSystemCrosDebugKey);
  if (value)
    return static_cast<CrosDebugFlag>(*value);

  // Fallback to disabled cros_debug.
  return CrosDebugFlag::kDisabled;
}

std::string ModelName() {
  std::string model_name;

  if (Context::Get()->cros_config()->GetString(
          kCrosConfigModelNamePath, kCrosConfigModelNameKey, &model_name))
    return model_name;

  // Fallback to sys_info.
  return base::SysInfo::GetLsbReleaseBoard();
}

}  // namespace runtime_probe
