// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CAMERA_DIAGNOSTICS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CAMERA_DIAGNOSTICS_H_

#include <camera/mojo/camera_diagnostics.mojom.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

namespace diagnostics {

// Fake implementation of CameraDiagnostics.
class FakeCameraDiagnostics
    : public ::cros::camera_diag::mojom::CameraDiagnostics {
 public:
  FakeCameraDiagnostics();
  FakeCameraDiagnostics(const FakeCameraDiagnostics&) = delete;
  FakeCameraDiagnostics& operator=(const FakeCameraDiagnostics&) = delete;
  ~FakeCameraDiagnostics() override;

  // Modifiers.
  mojo::Receiver<::cros::camera_diag::mojom::CameraDiagnostics>& receiver() {
    return receiver_;
  }

  void SetFrameAnalysisResult(
      ::cros::camera_diag::mojom::FrameAnalysisResultPtr frame_analysis_result);

 private:
  // ::cros::camera_diag::mojom::CameraDiagnostics overrides.
  void RunFrameAnalysis(
      ::cros::camera_diag::mojom::FrameAnalysisConfigPtr config,
      RunFrameAnalysisCallback callback) override;

  ::cros::camera_diag::mojom::FrameAnalysisResultPtr frame_analysis_result_;

  // Mojo receiver for binding pipe.
  mojo::Receiver<::cros::camera_diag::mojom::CameraDiagnostics> receiver_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_CAMERA_DIAGNOSTICS_H_
