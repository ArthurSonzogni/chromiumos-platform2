// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/input_device.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <pcrecpp.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

namespace {
constexpr auto kInputDevicesPath = "/proc/bus/input/devices";
const pcrecpp::RE kEventPatternRe(R"(event[\d]+)");

using FieldType = std::pair<std::string, std::string>;

const std::vector<FieldType> kTouchscreenI2cFields = {
    {"name", "name"}, {"product", "hw_version"}, {"fw_version", "fw_version"}};
const std::map<std::string, std::string> kTouchscreenI2cDriverToVid = {
    {"elants_i2c", "04f3"}, {"raydium_ts", "27a3"}, {"atmel_ext_ts", "03eb"}};

void AppendInputDevice(base::Value* list_value, base::Value&& value) {
  const auto* sysfs = value.FindStringKey("sysfs");
  if (sysfs) {
    const auto path = base::StringPrintf("/sys%s", sysfs->c_str());
    value.RemoveKey("sysfs");
    value.SetStringKey("path", path);
  }
  list_value->Append(std::move(value));
}

base::Value LoadInputDevices() {
  base::Value results(base::Value::Type::LIST);
  std::string input_devices_str;
  if (!base::ReadFileToString(base::FilePath(kInputDevicesPath),
                              &input_devices_str)) {
    LOG(ERROR) << "Failed to read " << kInputDevicesPath << ".";
    return results;
  }

  base::Value data;
  auto input_devices_lines = base::SplitStringPiece(
      input_devices_str, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& line : input_devices_lines) {
    if (line.length() < 3) {
      DCHECK_EQ(line.length(), 0);
      continue;
    }
    const auto& content = line.substr(3);
    base::StringPairs keyVals;
    switch (const auto prefix = line[0]; prefix) {
      case 'I': {
        if (data.is_dict() && !data.DictEmpty()) {
          AppendInputDevice(&results, std::move(data));
        }
        if (!base::SplitStringIntoKeyValuePairs(content, '=', ' ', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (I).";
          return base::Value(base::Value::Type::LIST);
        }
        data = base::Value(base::Value::Type::DICTIONARY);
        for (const auto& [key, value] : keyVals) {
          data.SetStringKey(base::ToLowerASCII(key), value);
        }
        break;
      }
      case 'N':
      case 'S': {
        if (!base::SplitStringIntoKeyValuePairs(content, '=', '\n', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (N/S).";
          return base::Value(base::Value::Type::LIST);
        }
        const auto& [key, value] = keyVals[0];
        data.SetStringKey(base::ToLowerASCII(key),
                          base::TrimString(value, "\"", base::TRIM_ALL));
        break;
      }
      case 'H': {
        if (!base::SplitStringIntoKeyValuePairs(content, '=', '\n', &keyVals)) {
          LOG(ERROR) << "Failed to parse input devices (H).";
          return base::Value(base::Value::Type::LIST);
        }
        const auto& value = keyVals[0].second;
        const auto& handlers = base::SplitStringPiece(
            value, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
        for (const auto& handler : handlers) {
          if (kEventPatternRe.FullMatch(handler.as_string())) {
            data.SetStringKey("event", handler);
            break;
          }
        }
        break;
      }
      default: {
        break;
      }
    }
  }
  if (!data.DictEmpty()) {
    AppendInputDevice(&results, std::move(data));
  }
  return results;
}

std::string GetDriverName(const base::FilePath& node_path) {
  const auto driver_path = node_path.Append("driver");
  const auto real_driver_path = base::MakeAbsoluteFilePath(driver_path);
  if (real_driver_path.value().length() == 0)
    return "";
  const auto driver_name = real_driver_path.BaseName().value();
  return driver_name;
}

void FixTouchscreenI2cDevices(base::Value* devices) {
  for (auto& device : devices->GetList()) {
    const auto* path = device.FindStringKey("path");
    if (!path)
      continue;

    if (const auto* vid_old = device.FindStringKey("vendor");
        vid_old && *vid_old != "0000")
      continue;

    const auto node_path = base::FilePath{*path}.Append("device");
    const auto driver_name = GetDriverName(node_path);
    const auto entry = kTouchscreenI2cDriverToVid.find(driver_name);
    if (entry == kTouchscreenI2cDriverToVid.end())
      continue;

    auto dict_value = MapFilesToDict(node_path, kTouchscreenI2cFields, {});
    if (!dict_value) {
      DVLOG(1) << "touchscreen_i2c-specific fields do not exist on node \""
               << node_path << "\"";
      continue;
    }

    device.SetStringKey("vendor", entry->second);
    device.MergeDictionary(&*dict_value);
  }
}

}  // namespace

InputDeviceFunction::DataType InputDeviceFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper to retrieve sysfs results.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  return DataType(json_output->TakeList());
}

int InputDeviceFunction::EvalInHelper(std::string* output) const {
  auto results = LoadInputDevices();
  FixTouchscreenI2cDevices(&results);

  if (!base::JSONWriter::Write(results, output)) {
    LOG(ERROR) << "Failed to serialize usb probed result to json string";
    return -1;
  }
  return 0;
}

}  // namespace runtime_probe
