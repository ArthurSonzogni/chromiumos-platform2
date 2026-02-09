// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/tests/camera_diagnostics_tests_fixture.h"

#include <cstdint>
#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/fake/simple_fake_service_manager.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/camera_diagnostics_helpers.h"
#include "diagnostics/camera_diagnostics_mojo_manager.h"
#include "diagnostics/camera_diagnostics_server.h"
#include "diagnostics/tests/fake_cros_camera_controller.h"

namespace cros::tests {

namespace {
constexpr uint32_t kCameraDiagUid = 603;
}  // namespace

CameraDiagnosticsTestsFixture::CameraDiagnosticsTestsFixture()
    : thread_("CamDiagFixture"), running_analysis_cv_(&running_analysis_lock_) {
  CHECK(thread_.Start());
}

CameraDiagnosticsTestsFixture::~CameraDiagnosticsTestsFixture() {
  if (thread_.IsCurrentThread()) {
    ResetOnThread();
  } else {
    thread_.PostTaskSync(
        FROM_HERE, base::BindOnce(&CameraDiagnosticsTestsFixture::ResetOnThread,
                                  base::Unretained(this)));
  }
}

void CameraDiagnosticsTestsFixture::ResetOnThread() {
  CHECK(thread_.IsCurrentThread());
  diag_server_.reset();
  camera_controller_.reset();
  diag_remote_.reset();
  service_manager_remote_.reset();
  mojo_manager_.reset();
  mojo_service_manager_.reset();
}

void CameraDiagnosticsTestsFixture::SetUp(const DiagFixtureOptions& options) {
  thread_.PostTaskSync(
      FROM_HERE, base::BindOnce(&CameraDiagnosticsTestsFixture::SetUpOnThread,
                                base::Unretained(this), options));
}

void CameraDiagnosticsTestsFixture::SetUpOnThread(
    const DiagFixtureOptions& options) {
  CHECK(thread_.IsCurrentThread());
  // Setup Mojo
  mojo_manager_ = std::make_unique<CameraDiagnosticsMojoManager>();
  mojo_service_manager_ = std::make_unique<
      chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>();
  mojo_manager_->SetMojoServiceManagerForTest(
      mojo_service_manager_->AddNewPipeAndPassRemote(kCameraDiagUid));

  // Start diagnostics server.
  diag_server_ = std::make_unique<CameraDiagnosticsServer>(mojo_manager_.get());

  if (options.enable_cros_camera) {
    // Start fake CrosCameraController. Equivalent to having cros-camera
    // running.
    camera_controller_ = std::make_unique<FakeCrosCameraController>(
        mojo_service_manager_->AddNewPipeAndPassRemote(kCameraDiagUid));
    camera_controller_->Initialize();
  }

  // Setup fixture's own mojo service manager remote.
  service_manager_remote_.Bind(
      mojo_service_manager_->AddNewPipeAndPassRemote(kCameraDiagUid));

  // Connect to camera diagnostics.
  service_manager_remote_->Request(
      chromeos::mojo_services::kCrosCameraDiagnostics, std::nullopt,
      diag_remote_.BindNewPipeAndPassReceiver().PassPipe());
}

cros::camera_diag::mojom::FrameAnalysisResultPtr
CameraDiagnosticsTestsFixture::RunFrameAnalysis(uint32_t duration_ms) {
  base::AutoLock lock(running_analysis_lock_);
  thread_.PostTaskAsync(
      FROM_HERE,
      base::BindOnce(&CameraDiagnosticsTestsFixture::RunFrameAnalysisOnThread,
                     base::Unretained(this), duration_ms));
  const uint32_t kDiagSlackTimeMs = 1 * 1000;  // Extra time to wait.
  running_analysis_cv_.TimedWait(
      base::Milliseconds(duration_ms + kDiagSlackTimeMs));

  return std::move(frame_analysis_result_);
}

void CameraDiagnosticsTestsFixture::RunFrameAnalysisOnThread(
    uint32_t duration_ms) {
  CHECK(thread_.IsCurrentThread());
  auto config = cros::camera_diag::mojom::FrameAnalysisConfig::New();
  config->client_type = cros::camera_diag::mojom::ClientType::kTest;
  config->duration_ms = duration_ms;
  diag_remote_->RunFrameAnalysis(
      std::move(config),
      base::BindOnce(&CameraDiagnosticsTestsFixture::OnDiagnosticsResult,
                     base::Unretained(this)));
}

void CameraDiagnosticsTestsFixture::OnDiagnosticsResult(
    cros::camera_diag::mojom::FrameAnalysisResultPtr result) {
  LOGF(INFO) << "Received the diagnostics result";
  switch (result->which()) {
    case cros::camera_diag::mojom::FrameAnalysisResult::Tag::kError:
      LOGF(INFO) << "Diagnostics Error: " << result->get_error();
      break;
    case cros::camera_diag::mojom::FrameAnalysisResult::Tag::kRes:
      LOGF(INFO) << "Full result: "
                 << DiagnosticsResultToJsonString(result->get_res());
      break;
    default:
      NOTREACHED();
  }
  frame_analysis_result_ = std::move(result);
  base::AutoLock lock(running_analysis_lock_);
  running_analysis_cv_.Signal();
}

FakeCrosCameraController* CameraDiagnosticsTestsFixture::GetCameraController() {
  return camera_controller_.get();
}

}  // namespace cros::tests
