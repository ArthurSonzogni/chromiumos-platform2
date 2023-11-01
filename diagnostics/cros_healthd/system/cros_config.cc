// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/cros_config.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/path_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/service_config.h"

namespace diagnostics {
namespace {

namespace paths = paths::cros_config;

base::FilePath GetCrosConfigFilePath(bool test_cros_config_,
                                     const base::FilePath& path) {
  const auto& root = test_cros_config_ ? paths::kTestRoot : paths::kRoot;
  return GetRootDir().Append(root.ToPath()).Append(path);
}

}  // namespace

CrosConfig::CrosConfig(const ServiceConfig& service_config)
    : test_cros_config_(service_config.test_cros_config) {}

CrosConfig::~CrosConfig() = default;

std::optional<std::string> CrosConfig::Get(const base::FilePath& path) {
  base::FilePath file = GetCrosConfigFilePath(test_cros_config_, path);

  std::string res;
  if (!base::ReadFileToString(file, &res)) {
    return std::nullopt;
  }
  return res;
}

std::optional<std::string> CrosConfig::Get(const PathLiteral& path) {
  return Get(path.ToPath());
}

}  // namespace diagnostics
