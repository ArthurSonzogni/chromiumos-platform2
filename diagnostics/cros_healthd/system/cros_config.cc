// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/cros_config.h"

#include <limits>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/types/expected.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/path_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/service_config.h"
#include "diagnostics/cros_healthd/system/cros_config_constants.h"

namespace diagnostics {
namespace {

namespace paths = paths::cros_config;

base::FilePath GetCrosConfigFilePath(bool test_cros_config_,
                                     const base::FilePath& path) {
  const auto& root = test_cros_config_ ? paths::kTestRoot : paths::kRoot;
  return GetRootDir().Append(root.ToPath()).Append(path);
}

base::unexpected<std::string> UnexpetedCrosConfig(
    const base::FilePath& cros_config_property,
    const std::string& expected_value,
    const std::optional<std::string>& got_value) {
  return base::unexpected("Expected cros_config property [" +
                          cros_config_property.value() + "] to be [" +
                          expected_value + "], but got [" +
                          got_value.value_or("") + "]");
}

}  // namespace

CrosConfig::CrosConfig(const ServiceConfig& service_config)
    : test_cros_config_(service_config.test_cros_config) {}

CrosConfig::~CrosConfig() = default;

std::optional<std::string> CrosConfig::Get(const base::FilePath& path) const {
  base::FilePath file = GetCrosConfigFilePath(test_cros_config_, path);

  std::string res;
  if (!base::ReadFileToString(file, &res)) {
    return std::nullopt;
  }
  return res;
}

std::optional<std::string> CrosConfig::Get(const PathLiteral& path) const {
  return Get(path.ToPath());
}

base::expected<void, std::string> CrosConfig::CheckExpectedCrosConfig(
    const base::FilePath& path, const std::string& expected) const {
  auto got = Get(path);
  if (got == expected) {
    return base::ok();
  }
  return UnexpetedCrosConfig(path, expected, got);
}

base::expected<void, std::string> CrosConfig::CheckExpectedCrosConfig(
    const PathLiteral& path, const std::string& expected) const {
  return CheckExpectedCrosConfig(path.ToPath(), expected);
}

base::expected<void, std::string> CrosConfig::CheckExpectedsCrosConfig(
    const base::FilePath& path,
    const std::vector<std::string>& expecteds) const {
  auto got = Get(path);
  for (const auto& expected : expecteds) {
    if (got == expected) {
      return base::ok();
    }
  }
  return UnexpetedCrosConfig(path, base::JoinString(expecteds, "] or ["), got);
}

base::expected<void, std::string> CrosConfig::CheckExpectedsCrosConfig(
    const PathLiteral& path, const std::vector<std::string>& expecteds) const {
  return CheckExpectedsCrosConfig(path.ToPath(), expecteds);
}

base::expected<void, std::string> CrosConfig::CheckTrueCrosConfig(
    const base::FilePath& path) const {
  return CheckExpectedCrosConfig(path, cros_config_value::kTrue);
}

base::expected<void, std::string> CrosConfig::CheckTrueCrosConfig(
    const PathLiteral& path) const {
  return CheckExpectedCrosConfig(path, cros_config_value::kTrue);
}

template <typename T>
base::expected<T, std::string> CrosConfig::GetUintCrosConfig(
    const base::FilePath& path, const std::string& type_name) const {
  auto got = Get(path);
  uint64_t value = 0;
  if (!got || !base::StringToUint64(got.value(), &value)) {
    return UnexpetedCrosConfig(path, type_name, got);
  }
  if (value > std::numeric_limits<T>::max()) {
    LOG(ERROR) << "Cros_config " << path.value() << " value " << got.value()
               << " overflow " << std::numeric_limits<T>::max();
    return UnexpetedCrosConfig(path, type_name, got);
  }
  return base::ok(static_cast<T>(value));
}

base::expected<uint8_t, std::string> CrosConfig::GetU8CrosConfig(
    const base::FilePath& path) const {
  return GetUintCrosConfig<uint8_t>(path, "uint8");
}

base::expected<uint32_t, std::string> CrosConfig::GetU32CrosConfig(
    const base::FilePath& path) const {
  return GetUintCrosConfig<uint32_t>(path, "uint32");
}

base::expected<uint64_t, std::string> CrosConfig::GetU64CrosConfig(
    const base::FilePath& path) const {
  return GetUintCrosConfig<uint64_t>(path, "uint64");
}

base::expected<uint8_t, std::string> CrosConfig::GetU8CrosConfig(
    const PathLiteral& path) const {
  return GetUintCrosConfig<uint8_t>(path.ToPath(), "uint8");
}

base::expected<uint32_t, std::string> CrosConfig::GetU32CrosConfig(
    const PathLiteral& path) const {
  return GetUintCrosConfig<uint32_t>(path.ToPath(), "uint32");
}

base::expected<uint64_t, std::string> CrosConfig::GetU64CrosConfig(
    const PathLiteral& path) const {
  return GetUintCrosConfig<uint64_t>(path.ToPath(), "uint64");
}

}  // namespace diagnostics
