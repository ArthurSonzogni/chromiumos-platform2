// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/feature_util.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

namespace vm_tools::concierge {

std::optional<int> FindIntValue(
    const std::map<std::string, std::string>& params, std::string key) {
  auto params_iter = params.find(key);
  if (params_iter == params.end()) {
    LOG(ERROR) << "Couldn't find the parameter: " << key;
    return std::nullopt;
  }

  int val;
  if (!base::StringToInt(params_iter->second, &val)) {
    LOG(ERROR) << "Failed to parse " << key
               << " parameter as int: " << params_iter->second;
    return std::nullopt;
  }
  return val;
}

}  // namespace vm_tools::concierge
