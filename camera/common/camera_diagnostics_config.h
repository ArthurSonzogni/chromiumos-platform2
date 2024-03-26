// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_COMMON_CAMERA_DIAGNOSTICS_CONFIG_H_
#define CAMERA_COMMON_CAMERA_DIAGNOSTICS_CONFIG_H_

#include <base/functional/callback_helpers.h>
#include <base/synchronization/lock.h>

#include "cros-camera/export.h"

namespace cros {

class CROS_CAMERA_EXPORT CameraDiagnosticsConfig {
 public:
  CameraDiagnosticsConfig() = default;
  CameraDiagnosticsConfig(const CameraDiagnosticsConfig&) = delete;
  CameraDiagnosticsConfig& operator=(const CameraDiagnosticsConfig&) = delete;
  ~CameraDiagnosticsConfig() = default;

  void SetFrameInterceptorState(bool new_state);

  bool IsFrameInterceptorEnabled();

 private:
  base::Lock lock_;

  bool frame_interceptor_enabled_ GUARDED_BY(lock_) = false;
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_DIAGNOSTICS_CONFIG_H_
