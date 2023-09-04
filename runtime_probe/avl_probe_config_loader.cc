// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/avl_probe_config_loader.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "runtime_probe/probe_config.h"
#include "runtime_probe/system/context.h"

namespace runtime_probe {

std::unique_ptr<ProbeConfig> AvlProbeConfigLoader::Load() const {
  for (const auto& file_path : GetPaths()) {
    if (base::PathExists(file_path)) {
      auto ret = ProbeConfig::FromFile(file_path);
      if (!(ret && ValidateProbeConfig(*ret))) {
        LOG(ERROR) << "Failed to load config: " << file_path.value();
        return nullptr;
      }
      return ret;
    }
  }
  return nullptr;
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

bool AvlProbeConfigLoader::ValidateProbeConfig(const ProbeConfig& config) {
  for (const auto& [category, components] : config) {
    if (!components)
      continue;
    for (const auto& [component, probe_statement] : *components) {
      if (!probe_statement || !probe_statement->probe_function())
        continue;
      auto function_name = probe_statement->probe_function()->GetFunctionName();
      if (std::count(kAllowedProbeFunctionNames.begin(),
                     kAllowedProbeFunctionNames.end(), function_name) == 0) {
        VLOG(1) << "Invalid probe function: " << function_name;
        return false;
      }
    }
  }
  return true;
}

}  // namespace runtime_probe
