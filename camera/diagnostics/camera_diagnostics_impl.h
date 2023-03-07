// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_IMPL_H_
#define CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_IMPL_H_

#include "camera/diagnostics/mojo/camera_diagnostics.mojom.h"

namespace cros {

class CameraDiagnosticsImpl : public cros::mojom::CameraDiagnostics {};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_CAMERA_DIAGNOSTICS_IMPL_H_
