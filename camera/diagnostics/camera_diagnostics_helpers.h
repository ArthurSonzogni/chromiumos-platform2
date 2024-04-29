// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_HELPERS_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_HELPERS_H_

#include <string>

#include <base/functional/callback.h>

#include "camera/mojo/camera_diagnostics.mojom.h"

namespace cros {

using CameraStartStreamingCallback =
    base::OnceCallback<void(camera_diag::mojom::StartStreamingResultPtr)>;

std::string DiagnosticsResultToJsonString(
    const cros::camera_diag::mojom::DiagnosticsResultPtr& result);

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_HELPERS_H_
