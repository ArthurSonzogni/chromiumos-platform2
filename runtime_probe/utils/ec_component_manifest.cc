// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ec_component_manifest.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos-config/libcros_config/cros_config.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace {

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

std::string ImageName() {
  std::string image_name;

  if (Context::Get()->cros_config()->GetString(
          kCrosConfigImageNamePath, kCrosConfigImageNameKey, &image_name))
    return image_name;

  LOG(ERROR) << "Failed to get \"" << kCrosConfigImageNamePath << " "
             << kCrosConfigImageNameKey << "\" from cros config";
  return "";
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
  if (!SetHexValue(dv.FindString("value"), ret.value)) {
    LOG(ERROR) << "Invalid or missing field: value";
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

base::FilePath EcComponentManifestReader::EcComponentManifestDefaultPath() {
  std::string image_name = ImageName();
  if (image_name.empty()) {
    return {};
  }
  return Context::Get()
      ->root_dir()
      .Append(kCmePath)
      .Append(image_name)
      .Append(kEcComponentManifestName);
}

std::optional<EcComponentManifest> EcComponentManifestReader::Read() {
  base::FilePath manifest_path = EcComponentManifestDefaultPath();
  if (manifest_path.empty()) {
    return std::nullopt;
  }
  return ReadFromFilePath(manifest_path);
}

std::optional<EcComponentManifest> EcComponentManifestReader::ReadFromFilePath(
    const base::FilePath& manifest_path) {
  std::string manifest_json;
  if (!base::ReadFileToString(manifest_path, &manifest_json)) {
    LOG(ERROR) << "Failed to read component manifest, path: " << manifest_path;
    return std::nullopt;
  }
  auto manifest_value = base::JSONReader::ReadDict(manifest_json);
  if (!manifest_value) {
    LOG(ERROR) << "Failed to parse component manifest, path: " << manifest_path;
    return std::nullopt;
  }
  auto manifest = EcComponentManifest::Create(*manifest_value);
  if (!manifest) {
    LOG(ERROR) << "Failed to parse component manifest, path: " << manifest_path;
    return std::nullopt;
  }
  return manifest;
}

}  // namespace runtime_probe
