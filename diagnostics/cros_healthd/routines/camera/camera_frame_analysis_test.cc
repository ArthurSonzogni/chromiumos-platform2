// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/camera/camera_frame_analysis.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <camera/mojo/camera_diagnostics.mojom.h>
#include <gtest/gtest.h>

#include "camera/mojo/camera_diagnostics.mojom-shared.h"
#include "diagnostics/cros_healthd/fake/fake_camera_diagnostics.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace camera_mojom = ::cros::camera_diag::mojom;
struct AnalyzerResults {
  camera_mojom::CameraIssue issue;
  std::optional<camera_mojom::AnalyzerStatus> privacy_shutter_test;
  std::optional<camera_mojom::AnalyzerStatus> dirty_lens_test;
  std::optional<camera_mojom::AnalyzerStatus> unknown_test;
};

// A helper function to create a camera diagnostics result.
camera_mojom::FrameAnalysisResultPtr NewCameraDiagnosticsResult(
    AnalyzerResults analyzer_results) {
  auto frame_diagnostics_result = camera_mojom::DiagnosticsResult::New();
  frame_diagnostics_result->suggested_issue = analyzer_results.issue;
  if (analyzer_results.privacy_shutter_test.has_value()) {
    frame_diagnostics_result->analyzer_results.push_back(
        camera_mojom::AnalyzerResult::New(
            camera_mojom::AnalyzerType::kPrivacyShutterSwTest,
            analyzer_results.privacy_shutter_test.value()));
  }
  if (analyzer_results.dirty_lens_test.has_value()) {
    frame_diagnostics_result->analyzer_results.push_back(
        camera_mojom::AnalyzerResult::New(
            camera_mojom::AnalyzerType::kDirtyLens,
            analyzer_results.dirty_lens_test.value()));
  }
  if (analyzer_results.unknown_test.has_value()) {
    frame_diagnostics_result->analyzer_results.push_back(
        camera_mojom::AnalyzerResult::New(
            camera_mojom::AnalyzerType::kUnknown,
            analyzer_results.unknown_test.value()));
  }
  return camera_mojom::FrameAnalysisResult::NewRes(
      std::move(frame_diagnostics_result));
}

// A helper function to create a frame analysis error.
camera_mojom::FrameAnalysisResultPtr NewFrameAnalysisError(
    camera_mojom::ErrorCode error_code) {
  return camera_mojom::FrameAnalysisResult::NewError(error_code);
}

mojom::RoutineStatePtr StartRoutineAndGetFinalState(
    BaseRoutineControl& routine) {
  routine.SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer;
  routine.SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine.Start();
  observer.WaitUntilRoutineFinished();
  return observer.state_.Clone();
}

std::pair<uint32_t, std::string> StartRoutineAndWaitForException(
    BaseRoutineControl& routine) {
  base::test::TestFuture<uint32_t, const std::string&> future;
  routine.SetOnExceptionCallback(future.GetCallback());
  routine.Start();
  return future.Take();
}

class CameraFrameAnalysisRoutineTest : public testing::Test {
 public:
  CameraFrameAnalysisRoutineTest() = default;
  CameraFrameAnalysisRoutineTest(const CameraFrameAnalysisRoutineTest&) =
      delete;
  CameraFrameAnalysisRoutineTest& operator=(
      const CameraFrameAnalysisRoutineTest&) = delete;

 protected:
  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();
  }

  void SetFrameAnalysisResult(camera_mojom::FrameAnalysisResultPtr result) {
    mock_context_.fake_mojo_service()
        ->fake_camera_diagnostics()
        .SetFrameAnalysisResult(std::move(result));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  CameraFrameAnalysisRoutine routine_{&mock_context_};
};

TEST_F(CameraFrameAnalysisRoutineTest, RoutineProgress) {
  routine_.SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  RoutineObserverForTesting observer;
  routine_.SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
  routine_.Start();

  task_environment_.FastForwardBy(base::Milliseconds(
      CameraFrameAnalysisRoutine::kExecutionDurationMilliseconds / 2));
  EXPECT_EQ(observer.state_->percentage, 50);
  EXPECT_TRUE(observer.state_->state_union->is_running());

  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kNone,
       .privacy_shutter_test = camera_mojom::AnalyzerStatus::kNotRun,
       .dirty_lens_test = camera_mojom::AnalyzerStatus::kNotRun}));
  observer.WaitUntilRoutineFinished();
  EXPECT_EQ(observer.state_->percentage, 100);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutinePassedWhenAllSubtestsPassed) {
  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kNone,
       .privacy_shutter_test = camera_mojom::AnalyzerStatus::kPassed,
       .dirty_lens_test = camera_mojom::AnalyzerStatus::kPassed}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);

  ASSERT_TRUE(
      result->state_union->get_finished()->detail->is_camera_frame_analysis());
  const auto& detail =
      result->state_union->get_finished()->detail->get_camera_frame_analysis();
  EXPECT_EQ(detail->privacy_shutter_open_test,
            mojom::CameraSubtestResult::kPassed);
  EXPECT_EQ(detail->lens_not_dirty_test, mojom::CameraSubtestResult::kPassed);
}

// The result of unknown analyzer type is ignored for forward compatibility.
TEST_F(CameraFrameAnalysisRoutineTest,
       RoutinePassedEvenAllSubtestsPassedExceptForUnknownSubtests) {
  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kNone,
       .privacy_shutter_test = camera_mojom::AnalyzerStatus::kPassed,
       .dirty_lens_test = camera_mojom::AnalyzerStatus::kPassed,
       .unknown_test = camera_mojom::AnalyzerStatus::kFailed}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

TEST_F(CameraFrameAnalysisRoutineTest,
       RoutineFailedWhenBlockedByPrivacyShutter) {
  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kPrivacyShutterOn,
       .privacy_shutter_test = camera_mojom::AnalyzerStatus::kFailed}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);

  ASSERT_TRUE(
      result->state_union->get_finished()->detail->is_camera_frame_analysis());
  const auto& detail =
      result->state_union->get_finished()->detail->get_camera_frame_analysis();
  EXPECT_EQ(detail->privacy_shutter_open_test,
            mojom::CameraSubtestResult::kFailed);
  EXPECT_EQ(
      detail->issue,
      mojom::CameraFrameAnalysisRoutineDetail::Issue::kBlockedByPrivacyShutter);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutineFailedWhenLensAreDirty) {
  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kDirtyLens,
       .dirty_lens_test = camera_mojom::AnalyzerStatus::kFailed}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);

  ASSERT_TRUE(
      result->state_union->get_finished()->detail->is_camera_frame_analysis());
  const auto& detail =
      result->state_union->get_finished()->detail->get_camera_frame_analysis();
  EXPECT_EQ(detail->lens_not_dirty_test, mojom::CameraSubtestResult::kFailed);
  EXPECT_EQ(detail->issue,
            mojom::CameraFrameAnalysisRoutineDetail::Issue::kLensAreDirty);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutineFailedWhenCameraServiceIsDown) {
  SetFrameAnalysisResult(NewCameraDiagnosticsResult(
      {.issue = camera_mojom::CameraIssue::kCameraServiceDown}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());
  EXPECT_FALSE(result->state_union->get_finished()->has_passed);

  ASSERT_TRUE(
      result->state_union->get_finished()->detail->is_camera_frame_analysis());
  const auto& detail =
      result->state_union->get_finished()->detail->get_camera_frame_analysis();
  EXPECT_EQ(detail->issue, mojom::CameraFrameAnalysisRoutineDetail::Issue::
                               kCameraServiceNotAvailable);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutineExceptionWithCameraClosed) {
  SetFrameAnalysisResult(
      NewFrameAnalysisError(camera_mojom::ErrorCode::kCameraClosed));

  const auto& [error, reason] = StartRoutineAndWaitForException(routine_);
  EXPECT_EQ(reason, "Camera frontend is not opened.");
  EXPECT_EQ(
      mojom::ToKnownEnumValue(static_cast<mojom::Exception::Reason>(error)),
      mojom::Exception::Reason::kCameraFrontendNotOpened);
}

TEST_F(CameraFrameAnalysisRoutineTest,
       RoutineExceptionWithAlreadyRunningAnalysis) {
  SetFrameAnalysisResult(
      NewFrameAnalysisError(camera_mojom::ErrorCode::kAlreadyRunningAnalysis));

  const auto& [error, reason] = StartRoutineAndWaitForException(routine_);
  EXPECT_EQ(reason, "Multiple frame analysis running.");
  EXPECT_EQ(
      mojom::ToKnownEnumValue(static_cast<mojom::Exception::Reason>(error)),
      mojom::Exception::Reason::kUnexpected);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutineExceptionWithInternalError) {
  SetFrameAnalysisResult(
      NewFrameAnalysisError(camera_mojom::ErrorCode::kDiagnosticsInternal));

  const auto& [error, reason] = StartRoutineAndWaitForException(routine_);
  EXPECT_EQ(reason, "Internal error.");
  EXPECT_EQ(
      mojom::ToKnownEnumValue(static_cast<mojom::Exception::Reason>(error)),
      mojom::Exception::Reason::kUnexpected);
}

TEST_F(CameraFrameAnalysisRoutineTest, RoutineExceptionWithUnknownError) {
  SetFrameAnalysisResult(
      NewFrameAnalysisError(camera_mojom::ErrorCode::kUnknown));

  const auto& [error, reason] = StartRoutineAndWaitForException(routine_);
  EXPECT_EQ(reason, "Internal error.");
  EXPECT_EQ(
      mojom::ToKnownEnumValue(static_cast<mojom::Exception::Reason>(error)),
      mojom::Exception::Reason::kUnexpected);
}

TEST_F(CameraFrameAnalysisRoutineTest,
       SubtestResultNotUnmappedEnumFieldWhenNoAnalyzerResults) {
  SetFrameAnalysisResult(
      NewCameraDiagnosticsResult({.issue = camera_mojom::CameraIssue::kNone}));

  const auto& result = StartRoutineAndGetFinalState(routine_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  ASSERT_TRUE(
      result->state_union->get_finished()->detail->is_camera_frame_analysis());
  const auto& detail =
      result->state_union->get_finished()->detail->get_camera_frame_analysis();
  EXPECT_NE(detail->privacy_shutter_open_test,
            mojom::CameraSubtestResult::kUnmappedEnumField);
  EXPECT_NE(detail->lens_not_dirty_test,
            mojom::CameraSubtestResult::kUnmappedEnumField);
}

}  // namespace
}  // namespace diagnostics
