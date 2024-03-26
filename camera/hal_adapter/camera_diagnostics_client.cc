// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hal_adapter/camera_diagnostics_client.h"

namespace cros {

CameraDiagnosticsClient::CameraDiagnosticsClient(
    CameraHalAdapter* camera_hal_adapter)
    : camera_hal_adapter_(camera_hal_adapter) {
  // Do not active diagnostics frame interception.
  camera_hal_adapter_->SetCameraDiagnosticsConfig(&camera_diagnostics_config_);
}

}  // namespace cros
