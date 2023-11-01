// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_

#include <optional>
#include <string>

namespace base {
class FilePath;
}

namespace diagnostics {
class PathLiteral;
struct ServiceConfig;

// Interface for accessing cros_config.
class CrosConfig {
 public:
  explicit CrosConfig(const ServiceConfig& service_config);
  CrosConfig(const CrosConfig&) = delete;
  CrosConfig& operator=(const CrosConfig&) = delete;
  ~CrosConfig();

  // Gets the cros config by path relative to cros config root.
  std::optional<std::string> Get(const base::FilePath& path);
  std::optional<std::string> Get(const PathLiteral& path);

 private:
  // If set, load chromeos-config from /run/chromeos-config/test.
  bool test_cros_config_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_
