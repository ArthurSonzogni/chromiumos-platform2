// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_TESTS_CAMERA_DIAGNOSTICS_TESTS_FIXTURE_H_
#define CAMERA_DIAGNOSTICS_TESTS_CAMERA_DIAGNOSTICS_TESTS_FIXTURE_H_

#include <memory>

#include <mojo_service_manager/fake/simple_fake_service_manager.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/camera_thread.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"
#include "diagnostics/camera_diagnostics_server.h"
#include "diagnostics/tests/fake_cros_camera_controller.h"

namespace cros::tests {

struct DiagFixtureOptions {
  bool enable_cros_camera = false;
};

class CameraDiagnosticsTestsFixture {
 public:
  CameraDiagnosticsTestsFixture();
  CameraDiagnosticsTestsFixture(const CameraDiagnosticsTestsFixture&) = delete;
  CameraDiagnosticsTestsFixture& operator=(
      const CameraDiagnosticsTestsFixture&) = delete;
  ~CameraDiagnosticsTestsFixture();

  // Blocking call.
  void SetUp(const DiagFixtureOptions& options);

  // Blocking call. Will return within |duration_ms| + 1sec.
  cros::camera_diag::mojom::FrameAnalysisResultPtr RunFrameAnalysis(
      uint32_t duration_ms);

  FakeCrosCameraController* GetCameraController();

 private:
  void SetUpOnThread(const DiagFixtureOptions& options);

  void RunFrameAnalysisOnThread(uint32_t duration_ms);

  void OnDiagnosticsResult(
      cros::camera_diag::mojom::FrameAnalysisResultPtr result);

  void ResetOnThread();

  CameraThread thread_;

  std::unique_ptr<CameraDiagnosticsMojoManager> mojo_manager_;

  std::unique_ptr<chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>
      mojo_service_manager_;

  std::unique_ptr<cros::CameraDiagnosticsServer> diag_server_;

  std::unique_ptr<FakeCrosCameraController> camera_controller_;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_remote_;

  mojo::Remote<cros::camera_diag::mojom::CameraDiagnostics> diag_remote_;

  cros::camera_diag::mojom::FrameAnalysisResultPtr frame_analysis_result_;

  base::Lock running_analysis_lock_;

  base::ConditionVariable running_analysis_cv_;
};
}  // namespace cros::tests

#endif  // CAMERA_DIAGNOSTICS_TESTS_CAMERA_DIAGNOSTICS_TESTS_FIXTURE_H_
