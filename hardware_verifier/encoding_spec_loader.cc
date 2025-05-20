// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/encoding_spec_loader.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/system/sys_info.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <libcrossystem/crossystem.h>

#include "hardware_verifier/hardware_verifier.pb.h"
#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

namespace {

constexpr char kCrosSystemCrosDebugKey[] = "cros_debug";
constexpr char kCrosConfigModelNamePath[] = "/";
constexpr char kCrosConfigModelNameKey[] = "name";
constexpr char kEncodingSpecDir[] = "etc/runtime_probe";
constexpr char kncodingSpecName[] = "encoding_spec.pb";
constexpr char kUsrLocal[] = "usr/local";

// Read the cros_debug crossystem property.
// Return true if cros_debug is enabled.
bool IsCrosDebugEnabled() {
  auto value = Context::Get()->crossystem()->VbGetSystemPropertyInt(
      kCrosSystemCrosDebugKey);
  return value == 1;
}

// Get the device model name.
std::string ModelName() {
  std::string model_name;

  if (Context::Get()->cros_config()->GetString(
          kCrosConfigModelNamePath, kCrosConfigModelNameKey, &model_name)) {
    return model_name;
  }

  LOG(ERROR) << "Failed to get \"" << kCrosConfigModelNamePath << " "
             << kCrosConfigModelNameKey << "\" from cros config";
  return "";
}

}  // namespace

std::vector<base::FilePath> EncodingSpecLoader::GetPaths() const {
  std::vector<base::FilePath> file_paths;
  std::string model_name = ModelName();
  if (model_name.empty()) {
    return file_paths;
  }

  if (IsCrosDebugEnabled()) {
    // Add paths under the stateful partition.
    auto encoding_spec_dir =
        Context::Get()->root_dir().Append(kUsrLocal).Append(kEncodingSpecDir);
    file_paths.push_back(
        encoding_spec_dir.Append(model_name).Append(kncodingSpecName));
  }
  auto encoding_spec_dir = Context::Get()->root_dir().Append(kEncodingSpecDir);
  file_paths.push_back(
      encoding_spec_dir.Append(model_name).Append(kncodingSpecName));
  return file_paths;
}

std::unique_ptr<EncodingSpec> EncodingSpecLoader::Load() const {
  for (const auto& file_path : GetPaths()) {
    if (base::PathExists(file_path)) {
      std::string content;
      if (!base::ReadFileToString(file_path, &content)) {
        LOG(ERROR) << "Failed to read encoding spec file: "
                   << file_path.value();
        continue;
      }

      auto encoding_spec = std::make_unique<EncodingSpec>();
      if (!encoding_spec->ParseFromString(content)) {
        LOG(ERROR) << "Failed to parse encoding spec from: "
                   << file_path.value();
        continue;
      }

      return encoding_spec;
    }
  }
  return nullptr;
}

}  // namespace hardware_verifier
