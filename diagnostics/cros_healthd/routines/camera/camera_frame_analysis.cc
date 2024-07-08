// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/camera/camera_frame_analysis.h"

#include <algorithm>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <camera/mojo/camera_diagnostics.mojom.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "camera/mojo/camera_diagnostics.mojom-shared.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace camera_mojom = ::cros::camera_diag::mojom;

// Duration for the frame analysis.
constexpr uint32_t kExecutionDurationMilliseconds = 5000;

mojom::CameraSubtestResult ConvertSubtestResult(
    camera_mojom::AnalyzerStatus status) {
  switch (status) {
    case camera_mojom::AnalyzerStatus::kNotRun:
      return mojom::CameraSubtestResult::kNotRun;
    case camera_mojom::AnalyzerStatus::kPassed:
      return mojom::CameraSubtestResult::kPassed;
    case camera_mojom::AnalyzerStatus::kFailed:
      return mojom::CameraSubtestResult::kFailed;
  }
}

mojom::CameraFrameAnalysisRoutineDetail::Issue ConvertIssue(
    camera_mojom::CameraIssue issue) {
  switch (issue) {
    case camera_mojom::CameraIssue::kNone:
      return mojom::CameraFrameAnalysisRoutineDetail::Issue::kNone;
    case camera_mojom::CameraIssue::kPrivacyShutterOn:
      return mojom::CameraFrameAnalysisRoutineDetail::Issue::
          kBlockedByPrivacyShutter;
    case camera_mojom::CameraIssue::kDirtyLens:
      return mojom::CameraFrameAnalysisRoutineDetail::Issue::kLensAreDirty;
    case camera_mojom::CameraIssue::kCameraServiceDown:
      return mojom::CameraFrameAnalysisRoutineDetail::Issue::
          kCameraServiceNotAvailable;
  }
}

}  // namespace

CameraFrameAnalysisRoutine::CameraFrameAnalysisRoutine(Context* context)
    : context_(context) {
  CHECK(context_);
}

CameraFrameAnalysisRoutine::~CameraFrameAnalysisRoutine() = default;

void CameraFrameAnalysisRoutine::OnStart() {
  SetRunningState();

  auto* mojo_service = context_->mojo_service();
  CHECK(mojo_service);

  auto* camera_diagnostics_service = mojo_service->GetCameraDiagnostics();
  if (!camera_diagnostics_service) {
    RaiseException("Failed to access camera diagnostics service.");
    return;
  }

  auto config = camera_mojom::FrameAnalysisConfig::New();
  config->client_type = camera_mojom::ClientType::kHealthd;
  config->duration_ms =
      std::clamp(kExecutionDurationMilliseconds,
                 camera_mojom::FrameAnalysisConfig::kMinDurationMs,
                 camera_mojom::FrameAnalysisConfig::kMaxDurationMs);

  camera_diagnostics_service->RunFrameAnalysis(
      std::move(config),
      mojo::WrapCallbackWithDropHandler(
          base::BindOnce(&CameraFrameAnalysisRoutine::OnResult,
                         weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&CameraFrameAnalysisRoutine::OnCallbackDropped,
                         weak_ptr_factory_.GetWeakPtr())));
}

void CameraFrameAnalysisRoutine::OnResult(
    camera_mojom::FrameAnalysisResultPtr result) {
  CHECK(result);
  switch (result->which()) {
    case camera_mojom::FrameAnalysisResult::Tag::kError: {
      OnErrorResult(result->get_error());
      return;
    }
    case camera_mojom::FrameAnalysisResult::Tag::kRes: {
      OnSuccessResult(result->get_res());
      return;
    }
  }
}

void CameraFrameAnalysisRoutine::OnErrorResult(
    camera_mojom::ErrorCode error_code) {
  LOG(WARNING) << "Received frame analysis error result: " << error_code;
  switch (error_code) {
    case camera_mojom::ErrorCode::kCameraClosed: {
      RaiseExceptionWithReason(
          mojom::Exception::Reason::kCameraFrontendNotOpened,
          "Camera frontend is not opened.");
      return;
    }
    case camera_mojom::ErrorCode::kAlreadyRunningAnalysis: {
      RaiseException("Multiple frame analysis running.");
      return;
    }
    // No need to disclose details to clients.
    case camera_mojom::ErrorCode::kUnknown:
    case camera_mojom::ErrorCode::kInvalidDuration:
    case camera_mojom::ErrorCode::kCrosCameraControllerNotRegistered:
    case camera_mojom::ErrorCode::kDiagnosticsInternal: {
      RaiseException("Internal error.");
      return;
    }
  }
}

void CameraFrameAnalysisRoutine::OnSuccessResult(
    const camera_mojom::DiagnosticsResultPtr& result) {
  auto routine_detail = mojom::CameraFrameAnalysisRoutineDetail::New();
  routine_detail->privacy_shutter_open_test =
      mojom::CameraSubtestResult::kNotRun;
  routine_detail->lens_not_dirty_test = mojom::CameraSubtestResult::kNotRun;

  routine_detail->issue = ConvertIssue(result->suggested_issue);
  for (const auto& analyzer_result : result->analyzer_results) {
    switch (analyzer_result->type) {
      case camera_mojom::AnalyzerType::kPrivacyShutterSwTest:
        routine_detail->privacy_shutter_open_test =
            ConvertSubtestResult(analyzer_result->status);
        break;
      case camera_mojom::AnalyzerType::kDirtyLens:
        routine_detail->lens_not_dirty_test =
            ConvertSubtestResult(analyzer_result->status);
        break;
      case camera_mojom::AnalyzerType::kUnknown:
        LOG(WARNING) << "Got unknown camera analyzer type with status="
                     << analyzer_result->status;
        break;
    }
  }
  bool has_passed = (routine_detail->issue ==
                     mojom::CameraFrameAnalysisRoutineDetail::Issue::kNone);
  SetFinishedState(has_passed, mojom::RoutineDetail::NewCameraFrameAnalysis(
                                   std::move(routine_detail)));
}

void CameraFrameAnalysisRoutine::OnCallbackDropped() {
  LOG(ERROR) << "Camera frame analysis callback dropped";
  RaiseException("Internal error.");
}

}  // namespace diagnostics
