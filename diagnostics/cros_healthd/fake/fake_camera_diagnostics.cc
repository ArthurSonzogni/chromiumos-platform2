// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_camera_diagnostics.h"

#include <utility>

#include <camera/mojo/camera_diagnostics.mojom.h>

namespace diagnostics {

FakeCameraDiagnostics::FakeCameraDiagnostics() = default;

FakeCameraDiagnostics::~FakeCameraDiagnostics() = default;

void FakeCameraDiagnostics::SetFrameAnalysisResult(
    ::cros::camera_diag::mojom::FrameAnalysisResultPtr frame_analysis_result) {
  frame_analysis_result_ = std::move(frame_analysis_result);
}

void FakeCameraDiagnostics::RunFrameAnalysis(
    ::cros::camera_diag::mojom::FrameAnalysisConfigPtr config,
    RunFrameAnalysisCallback callback) {
  std::move(callback).Run(frame_analysis_result_.Clone());
}

}  // namespace diagnostics
