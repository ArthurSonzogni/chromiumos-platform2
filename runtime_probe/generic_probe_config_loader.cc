// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <base/logging.h>

#include "runtime_probe/generic_probe_config_loader.h"

namespace runtime_probe {

std::optional<ProbeConfigData> GenericProbeConfigLoader::Load() const {
  if (CrosDebug() != CrosDebugFlag::kEnabled) {
    LOG(ERROR) << "Arbitrary probe config is only allowed with cros_debug=1";
    return std::nullopt;
  }
  return LoadProbeConfigDataFromFile(path_);
}

}  // namespace runtime_probe
