// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos-config/libcros_config/fake_cros_config.h"

#include <base/logging.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"

namespace brillo {

FakeCrosConfig::FakeCrosConfig() {}

FakeCrosConfig::~FakeCrosConfig() {}

void FakeCrosConfig::SetString(const std::string& path,
                               const std::string& property,
                               const std::string& val) {
  values_[PathProp{path, property}] = val;
}

bool FakeCrosConfig::GetString(const std::string& path,
                               const std::string& property,
                               std::string* val) {
  auto it = values_.find(PathProp{path, property});
  if (it == values_.end()) {
    CROS_CONFIG_LOG(WARNING) << "Cannot get path " << path << " property "
                             << property << ": <fake_error>";
    return false;
  }
  *val = it->second;

  return true;
}

bool FakeCrosConfig::GetDeviceIndex(int* device_index_out) {
  *device_index_out = 0;
  return true;
}

}  // namespace brillo
