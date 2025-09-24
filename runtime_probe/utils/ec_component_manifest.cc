// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ec_component_manifest.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace {

constexpr int kDefaultBytes = 1;
constexpr char kLowPowerProbeOnce[] = "low_power_probe_once";

template <typename T, typename U>
bool SetValue(const U& value, T& val) {
  if (!value) {
    return false;
  }
  val = *value;
  return true;
}

template <typename T>
bool SetHexValue(const std::string* value, T& val) {
  if (!value) {
    return false;
  }
  uint32_t val_;
  if (!base::HexStringToUInt(*value, &val_)) {
    return false;
  }
  if (val_ < std::numeric_limits<T>::min() ||
      std::numeric_limits<T>::max() < val_) {
    return false;
  }
  val = val_;
  return true;
}

template <typename T>
bool SetBytes(const std::string* value, T& val) {
  if (!value ||
      !base::StartsWith(*value, "0x", base::CompareCase::INSENSITIVE_ASCII)) {
    return false;
  }

  // Remove "0x".
  std::string hex_val = value->substr(2);

  std::vector<uint8_t> val_;
  if (!base::HexStringToBytes(hex_val, &val_)) {
    return false;
  }
  val = val_;
  return true;
}

template <typename T>
bool SetDict(const base::Value::Dict* dict, T& val) {
  if (!dict) {
    return false;
  }
  auto value_ = T::Create(*dict);
  if (!value_) {
    return false;
  }
  val = std::move(*value_);
  return true;
}

template <typename T>
bool SetList(const base::Value::List* list, std::vector<T>& vec) {
  if (!list) {
    return false;
  }
  std::vector<T> ret;
  for (const auto& item : *list) {
    if (!item.is_dict()) {
      return false;
    }
    auto value_ = T::Create(item.GetDict());
    if (!value_) {
      return false;
    }
    ret.push_back(std::move(*value_));
  }
  vec = std::move(ret);
  return true;
}

std::optional<std::string> GetEcProjectName() {
  std::string ec_project_name;

  if (Context::Get()->cros_config()->GetString(kCrosConfigImageNamePath,
                                               kCrosConfigImageNameKey,
                                               &ec_project_name)) {
    return ec_project_name;
  }

  LOG(ERROR) << "Failed to get \"" << kCrosConfigImageNamePath << " "
             << kCrosConfigImageNameKey << "\" from cros config";
  return std::nullopt;
}

bool SetBytesFromDict(const base::Value::Dict& dv,
                      std::string_view key,
                      std::string_view multi_byte_key,
                      std::string_view override_key,
                      std::optional<std::vector<uint8_t>>& out) {
  const std::string* override_value = dv.FindString(override_key);
  if (override_value) {
    if (!SetBytes(override_value, out)) {
      LOG(ERROR) << "Invalid field: " << override_key;
      return false;
    }
    return true;
  }

  const std::string* value = dv.FindString(key);
  const std::string* multi_byte_value = dv.FindString(multi_byte_key);
  if (value && multi_byte_value) {
    LOG(ERROR) << "Conflict field: " << key << " and " << multi_byte_key;
    return false;
  }
  if (value && !SetBytes(value, out)) {
    LOG(ERROR) << "Invalid field: " << key;
    return false;
  }
  if (multi_byte_value && !SetBytes(multi_byte_value, out)) {
    LOG(ERROR) << "Invalid field: " << multi_byte_key;
    return false;
  }
  return true;
}

}  // namespace

std::optional<EcComponentManifest::Component::I2c::Expect>
EcComponentManifest::Component::I2c::Expect::Create(
    const base::Value::Dict& dv) {
  EcComponentManifest::Component::I2c::Expect ret{};
  if (!SetHexValue(dv.FindString("reg"), ret.reg)) {
    LOG(ERROR) << "Invalid or missing field: reg";
    return std::nullopt;
  }

  auto write_data = dv.FindString("write_data");
  if (write_data && !SetBytes(write_data, ret.write_data)) {
    LOG(ERROR) << "Invalid field: write_data";
    return std::nullopt;
  }

  if (!SetBytesFromDict(dv, "value", "multi_byte_value", "override_value",
                        ret.value)) {
    return std::nullopt;
  }
  if (!SetBytesFromDict(dv, "mask", "multi_byte_mask", "override_mask",
                        ret.mask)) {
    return std::nullopt;
  }

  auto override_addr = dv.FindString("override_addr");
  if (override_addr) {
    uint8_t override_addr_val;
    if (!SetHexValue(override_addr, override_addr_val)) {
      LOG(ERROR) << "Invalid field: override_addr";
      return std::nullopt;
    }
    ret.override_addr = override_addr_val;
  }

  auto bytes = dv.FindInt("bytes");
  if (!bytes.has_value()) {
    SetValue(&kDefaultBytes, ret.bytes);
  } else if (!SetValue(bytes, ret.bytes)) {
    LOG(ERROR) << "Invalid field: bytes";
    return std::nullopt;
  }

  if (ret.value.has_value() && ret.value->size() != ret.bytes) {
    LOG(ERROR) << "Invalid field: the length of value is different from bytes";
    return std::nullopt;
  }
  if (ret.mask.has_value() && ret.mask->size() != ret.bytes) {
    LOG(ERROR) << "Invalid field: the length of mask is different from bytes";
    return std::nullopt;
  }
  return ret;
}

std::optional<EcComponentManifest::Component::I2c>
EcComponentManifest::Component::I2c::Create(const base::Value::Dict& dv) {
  EcComponentManifest::Component::I2c ret{};
  if (!SetValue(dv.FindInt("port"), ret.port)) {
    LOG(ERROR) << "Invalid or missing field: port";
    return std::nullopt;
  }
  if (!SetHexValue(dv.FindString("addr"), ret.addr)) {
    LOG(ERROR) << "Invalid or missing field: addr";
    return std::nullopt;
  }
  auto expect = dv.FindList("expect");
  if (expect && !SetList(expect, ret.expect)) {
    LOG(ERROR) << "Invalid field: expect";
    return std::nullopt;
  }
  return ret;
}

std::optional<EcComponentManifest::Component>
EcComponentManifest::Component::Create(const base::Value::Dict& dv) {
  EcComponentManifest::Component ret{};
  if (!SetValue(dv.FindString("component_type"), ret.component_type)) {
    LOG(ERROR) << "Invalid or missing field: component_type";
    return std::nullopt;
  }
  if (!SetValue(dv.FindString("component_name"), ret.component_name)) {
    LOG(ERROR) << "Invalid or missing field: component_name";
    return std::nullopt;
  }
  auto i2c = dv.FindDict("i2c");
  if (i2c && !SetDict(i2c, ret.i2c)) {
    LOG(ERROR) << "Invalid field: i2c";
    return std::nullopt;
  }

  auto probe = dv.FindString("probe");
  if (probe && *probe == kLowPowerProbeOnce) {
    ret.probe_strategy = ProbeStrategy::WAKE_UP;
  } else {
    ret.probe_strategy = ProbeStrategy::DEFAULT;
  }
  return ret;
}

std::optional<EcComponentManifest> EcComponentManifest::Create(
    const base::Value::Dict& dv) {
  EcComponentManifest ret{};
  if (!SetValue(dv.FindInt("manifest_version"), ret.manifest_version)) {
    LOG(ERROR) << "Invalid or missing field: manifest_version";
    return std::nullopt;
  }
  if (!SetValue(dv.FindString("ec_version"), ret.ec_version)) {
    LOG(ERROR) << "Invalid or missing field: ec_version";
    return std::nullopt;
  }
  if (!SetList(dv.FindList("component_list"), ret.component_list)) {
    LOG(ERROR) << "Invalid or missing field: component_list";
    return std::nullopt;
  }
  return ret;
}

EcComponentManifestReader::EcComponentManifestReader(
    std::string_view ec_version)
    : ec_version_(ec_version) {}

base::FilePath EcComponentManifestReader::EcComponentManifestDefaultPath()
    const {
  const auto ec_project_name = GetEcProjectName();
  if (!ec_project_name.has_value()) {
    return {};
  }
  return Context::Get()
      ->root_dir()
      .Append(kCmePath)
      .Append(*ec_project_name)
      .Append(kEcComponentManifestName);
}

std::optional<EcComponentManifest> EcComponentManifestReader::Read() const {
  base::FilePath manifest_path = EcComponentManifestDefaultPath();
  if (manifest_path.empty()) {
    return std::nullopt;
  }
  return ReadFromFilePath(manifest_path);
}

std::optional<EcComponentManifest> EcComponentManifestReader::ReadFromFilePath(
    const base::FilePath& manifest_path) const {
  std::string manifest_json;
  LOG(INFO) << "Reading component manifest from: " << manifest_path.value();
  if (!base::ReadFileToString(manifest_path, &manifest_json)) {
    LOG(ERROR) << "Failed to read component manifest, path: " << manifest_path;
    return std::nullopt;
  }
  auto manifest_value = base::JSONReader::ReadDict(
      manifest_json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!manifest_value) {
    LOG(ERROR) << "Failed to parse component manifest, path: " << manifest_path;
    return std::nullopt;
  }
  auto manifest = EcComponentManifest::Create(*manifest_value);
  if (!manifest) {
    LOG(ERROR) << "Failed to parse component manifest, path: " << manifest_path;
    return std::nullopt;
  }

  if (ec_version_ != manifest->ec_version) {
    LOG(ERROR) << "Current EC version \"" << ec_version_
               << "\" doesn't match manifest version \"" << manifest->ec_version
               << "\".";
    return std::nullopt;
  }

  return manifest;
}

}  // namespace runtime_probe
