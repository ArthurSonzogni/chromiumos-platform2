// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_
#define CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_

#include "common/camera_diagnostics_config.h"
#include "hal_adapter/camera_hal_adapter.h"

namespace cros {

// A wrapper for mojo call to camera diagnostics service.
class CameraDiagnosticsClient {
 public:
  explicit CameraDiagnosticsClient(CameraHalAdapter* camera_hal_adapter);
  CameraDiagnosticsClient(const CameraDiagnosticsClient&) = delete;
  CameraDiagnosticsClient& operator=(const CameraDiagnosticsClient&) = delete;
  ~CameraDiagnosticsClient() = default;

 private:
  CameraDiagnosticsConfig camera_diagnostics_config_;

  CameraHalAdapter* camera_hal_adapter_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_DIAGNOSTICS_CLIENT_H_
