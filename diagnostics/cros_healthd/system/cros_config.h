// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <base/types/expected.h>

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
  std::optional<std::string> Get(const base::FilePath& path) const;
  std::optional<std::string> Get(const PathLiteral& path) const;

  // Gets cros config which has an expected value. Returns error if value
  // doesn't match.
  base::expected<void, std::string> CheckExpectedCrosConfig(
      const base::FilePath& path, const std::string& expected) const;
  base::expected<void, std::string> CheckExpectedCrosConfig(
      const PathLiteral& path, const std::string& expected) const;
  // Same as above but check if it match any value in a list.
  base::expected<void, std::string> CheckExpectedsCrosConfig(
      const base::FilePath& path,
      const std::vector<std::string>& expecteds) const;
  base::expected<void, std::string> CheckExpectedsCrosConfig(
      const PathLiteral& path, const std::vector<std::string>& expecteds) const;

  // Gets cros config which is a true boolean value. Returns error if it isn't.
  base::expected<void, std::string> CheckTrueCrosConfig(
      const base::FilePath& path) const;
  base::expected<void, std::string> CheckTrueCrosConfig(
      const PathLiteral& path) const;

  // Gets cros config which is a unsigned integer value. Returns its value if it
  // is an integer. Returns error if it isn't. Supports different integer types.
  base::expected<uint8_t, std::string> GetU8CrosConfig(
      const base::FilePath& path) const;
  base::expected<uint32_t, std::string> GetU32CrosConfig(
      const base::FilePath& path) const;
  base::expected<uint64_t, std::string> GetU64CrosConfig(
      const base::FilePath& path) const;
  base::expected<uint8_t, std::string> GetU8CrosConfig(
      const PathLiteral& path) const;
  base::expected<uint32_t, std::string> GetU32CrosConfig(
      const PathLiteral& path) const;
  base::expected<uint64_t, std::string> GetU64CrosConfig(
      const PathLiteral& path) const;

 private:
  template <typename T>
  base::expected<T, std::string> GetUintCrosConfig(
      const base::FilePath& path, const std::string& type_name) const;

  // If set, load chromeos-config from /run/chromeos-config/test.
  bool test_cros_config_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CROS_CONFIG_H_
