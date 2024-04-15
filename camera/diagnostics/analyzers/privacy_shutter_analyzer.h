// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_ANALYZERS_PRIVACY_SHUTTER_ANALYZER_H_
#define CAMERA_DIAGNOSTICS_ANALYZERS_PRIVACY_SHUTTER_ANALYZER_H_

#include "camera/diagnostics/analyzers/frame_analyzer.h"
#include "camera/mojo/camera_diagnostics.mojom.h"

namespace cros {

// Not thread-safe.
class PrivacyShutterAnalyzer final : public FrameAnalyzer {
 public:
  PrivacyShutterAnalyzer() = default;

  PrivacyShutterAnalyzer(const PrivacyShutterAnalyzer&) = delete;
  PrivacyShutterAnalyzer& operator=(const PrivacyShutterAnalyzer&) = delete;
  ~PrivacyShutterAnalyzer() final = default;

  void AnalyzeFrame(const camera_diag::mojom::CameraFramePtr& frame) final;

  camera_diag::mojom::AnalyzerResultPtr GetResult() final;

 private:
  bool IsValidFrame(const camera_diag::mojom::CameraFramePtr&);

  bool DetectPrivacyShutter(const camera_diag::mojom::CameraFramePtr& frame);

  const camera_diag::mojom::AnalyzerType type_ =
      camera_diag::mojom::AnalyzerType::kPrivacyShutterSwTest;

  // Individual frame results.
  int analyzed_frames_count_ = 0;
  // Keeps the latest consecutive shutter detection count.
  int shutter_detected_on_frames_count_ = 0;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_ANALYZERS_PRIVACY_SHUTTER_ANALYZER_H_
