// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_ANALYZERS_FRAME_ANALYZER_H_
#define CAMERA_DIAGNOSTICS_ANALYZERS_FRAME_ANALYZER_H_

#include "camera/mojo/camera_diagnostics.mojom.h"

namespace cros {

class FrameAnalyzer {
 public:
  virtual ~FrameAnalyzer() = default;

  virtual void AnalyzeFrame(
      const camera_diag::mojom::CameraFramePtr& frame) = 0;

  virtual camera_diag::mojom::AnalyzerResultPtr GetResult() = 0;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_ANALYZERS_FRAME_ANALYZER_H_
