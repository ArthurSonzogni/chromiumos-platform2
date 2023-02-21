// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/ssfc_probe_config_loader.h"

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "runtime_probe/probe_config.h"
#include "runtime_probe/system/context.h"

namespace runtime_probe {

std::optional<ProbeConfig> SsfcProbeConfigLoader::Load() const {
  for (const auto& file_path : GetPaths()) {
    auto ret = ProbeConfig::FromFile(file_path);
    if (ret) {
      return ret;
    }
  }
  return {};
}

std::vector<base::FilePath> SsfcProbeConfigLoader::GetPaths() const {
  std::vector<base::FilePath> file_paths;
  std::string model_name = ModelName();
  if (CrosDebug() == CrosDebugFlag::kEnabled) {
    // Add paths under the stateful partition.
    auto probe_config_dir = Context::Get()->root_dir().Append(kUsrLocal).Append(
        kRuntimeProbeConfigDir);
    file_paths.push_back(
        probe_config_dir.Append(model_name).Append(kSsfcProbeConfigName));
  }
  auto probe_config_dir =
      Context::Get()->root_dir().Append(kRuntimeProbeConfigDir);
  file_paths.push_back(
      probe_config_dir.Append(model_name).Append(kSsfcProbeConfigName));
  return file_paths;
}

}  // namespace runtime_probe
