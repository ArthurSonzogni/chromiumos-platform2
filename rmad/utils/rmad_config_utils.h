// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_RMAD_CONFIG_UTILS_H_
#define RMAD_UTILS_RMAD_CONFIG_UTILS_H_

#include "rmad/rmad_config.pb.h"

namespace rmad {

class RmadConfigUtils {
 public:
  RmadConfigUtils() = default;
  virtual ~RmadConfigUtils() = default;

  // Get the RmadConfig.
  virtual const std::optional<RmadConfig>& GetConfig() const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_RMAD_CONFIG_UTILS_H_
