// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_properties.h"

#include <optional>
#include <string>

#include <base/files/file_util.h>

namespace {

std::optional<std::string> ReadProperty(const base::FilePath& property_path) {
  std::optional<std::string> ret = std::nullopt;
  if (std::string value; base::ReadFileToString(property_path, &value)) {
    ret = value;
  }
  return ret;
}

std::string StringIsExpectedDescription(const std::optional<std::string>& value,
                                        const std::string& expect) {
  return (value.has_value() && value.value() == expect) ? "Yes" : "No";
}

}  // namespace

namespace rmad {

std::string GetHasTouchscreenDescription(const base::FilePath& root_path) {
  base::FilePath path = root_path.Append(kCrosHardwarePropertiesPath)
                            .Append(kCrosHardwarePropertiesHasTouchscreenKey);
  std::optional<std::string> value = ReadProperty(path);
  return std::string("Touchscreen:") +
         StringIsExpectedDescription(value, "true");
}

}  // namespace rmad
