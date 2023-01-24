// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "runtime_probe/avl_probe_config_loader.h"
#include "runtime_probe/system/context.h"

namespace runtime_probe {

std::optional<ProbeConfigData> AvlProbeConfigLoader::Load() const {
  for (const auto& file_path : GetPaths()) {
    auto ret = LoadProbeConfigDataFromFile(file_path);
    if (ret) {
      return ret;
    }
  }
  return std::nullopt;
}

std::vector<base::FilePath> AvlProbeConfigLoader::GetPaths() const {
  std::vector<base::FilePath> file_paths;
  std::string model_name = ModelName();
  if (CrosDebug() == CrosDebugFlag::kEnabled) {
    // Add paths under the stateful partition.
    auto probe_config_dir = Context::Get()->root_dir().Append(kUsrLocal).Append(
        kRuntimeProbeConfigDir);
    file_paths.push_back(
        probe_config_dir.Append(model_name).Append(kAvlProbeConfigName));
    file_paths.push_back(probe_config_dir.Append(kAvlProbeConfigName));
  }
  auto probe_config_dir =
      Context::Get()->root_dir().Append(kRuntimeProbeConfigDir);
  file_paths.push_back(
      probe_config_dir.Append(model_name).Append(kAvlProbeConfigName));
  file_paths.push_back(probe_config_dir.Append(kAvlProbeConfigName));
  return file_paths;
}

}  // namespace runtime_probe
