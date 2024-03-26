// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/camera_diagnostics_config.h"

namespace cros {

void CameraDiagnosticsConfig::SetFrameInterceptorState(bool new_state) {
  base::AutoLock lock(lock_);
  frame_interceptor_enabled_ = new_state;
}

bool CameraDiagnosticsConfig::IsFrameInterceptorEnabled() {
  base::AutoLock lock(lock_);
  return frame_interceptor_enabled_;
}

}  // namespace cros
