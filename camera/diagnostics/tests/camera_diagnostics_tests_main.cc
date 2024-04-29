// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <base/test/test_timeouts.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/flag_helper.h>
#include <ml_core/dlc/dlc_client.h>
#include <ml_core/dlc/dlc_ids.h>
#include <ml_core/dlc/dlc_loader.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common_types.h"
#include "diagnostics/tests/camera_diagnostics_tests_fixture.h"

// gtest's internal typedef of None and Bool conflicts with the None and Bool
// macros in X11/X.h (https://github.com/google/googletest/issues/371).
// X11/X.h is pulled in by the GL headers we include.
#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#pragma pop_macro("None")
#pragma pop_macro("Bool")

namespace cros::tests {

namespace {
constexpr int64_t kFrameAnalysisDurationMsDefault = 5 * 1000;
}

class CameraDiagTest : public ::testing::TestWithParam<Size> {};

INSTANTIATE_TEST_SUITE_P(,
                         CameraDiagTest,
                         ::testing::Values(Size{1920, 1080},  // 16:9
                                           Size{1600, 1200},  // 4:3
                                           Size{640, 360}));  // 16:9, small

TEST_P(CameraDiagTest, CameraServiceDown) {
  CameraDiagnosticsTestsFixture fixture;
  fixture.SetUp(DiagFixtureOptions{.enable_cros_camera = false});

  base::ElapsedTimer timer;

  auto result = fixture.RunFrameAnalysis(kFrameAnalysisDurationMsDefault);

  int64_t time_spent_ms = timer.Elapsed().InMilliseconds();
  ASSERT_LE(time_spent_ms, kFrameAnalysisDurationMsDefault);

  ASSERT_FALSE(result.is_null());
  ASSERT_TRUE(result->is_res());
  ASSERT_EQ(result->get_res()->suggested_issue,
            cros::camera_diag::mojom::CameraIssue::kCameraServiceDown);
}

TEST_P(CameraDiagTest, CameraClosed) {
  CameraDiagnosticsTestsFixture fixture;
  fixture.SetUp(DiagFixtureOptions{.enable_cros_camera = true});

  base::ElapsedTimer timer;

  auto result = fixture.RunFrameAnalysis(kFrameAnalysisDurationMsDefault);

  int64_t time_spent_ms = timer.Elapsed().InMilliseconds();
  ASSERT_LE(time_spent_ms, kFrameAnalysisDurationMsDefault);

  ASSERT_FALSE(result.is_null());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->get_error(),
            cros::camera_diag::mojom::ErrorCode::kCameraClosed);
}

TEST_P(CameraDiagTest, PrivacyShutterOn) {
  const Size& stream_size = GetParam();

  CameraDiagnosticsTestsFixture fixture;
  fixture.SetUp(DiagFixtureOptions{.enable_cros_camera = true});

  // Open camera.
  auto camera_controller = fixture.GetCameraController();
  auto stream = camera_diag::mojom::CameraStream::New();
  stream->width = stream_size.width;
  stream->height = stream_size.height;
  stream->pixel_format = camera_diag::mojom::PixelFormat::kYuv420;
  camera_controller->OpenCamera(std::move(stream), FrameType::kBlack);

  base::ElapsedTimer timer;

  auto result = fixture.RunFrameAnalysis(kFrameAnalysisDurationMsDefault);

  int64_t time_spent_ms = timer.Elapsed().InMilliseconds();
  ASSERT_LE(time_spent_ms, kFrameAnalysisDurationMsDefault);

  ASSERT_FALSE(result.is_null());
  ASSERT_TRUE(result->is_res());
  ASSERT_EQ(result->get_res()->suggested_issue,
            cros::camera_diag::mojom::CameraIssue::kPrivacyShutterOn);
}

#if USE_DLC
TEST_P(CameraDiagTest, DirtyLensDetected) {
  const Size& stream_size = GetParam();

  CameraDiagnosticsTestsFixture fixture;
  fixture.SetUp(DiagFixtureOptions{.enable_cros_camera = true});

  // Open camera.
  auto camera_controller = fixture.GetCameraController();
  auto stream = camera_diag::mojom::CameraStream::New();
  stream->width = stream_size.width;
  stream->height = stream_size.height;
  stream->pixel_format = camera_diag::mojom::PixelFormat::kYuv420;
  camera_controller->OpenCamera(std::move(stream), FrameType::kBlurry);

  base::ElapsedTimer timer;

  auto result = fixture.RunFrameAnalysis(kFrameAnalysisDurationMsDefault);

  int64_t time_spent_ms = timer.Elapsed().InMilliseconds();
  ASSERT_LE(time_spent_ms, kFrameAnalysisDurationMsDefault);

  ASSERT_FALSE(result.is_null());
  ASSERT_TRUE(result->is_res());
  ASSERT_EQ(result->get_res()->suggested_issue,
            cros::camera_diag::mojom::CameraIssue::kDirtyLens);
}
#endif  // USE_DLC

TEST_P(CameraDiagTest, NoIssue) {
  const Size& stream_size = GetParam();

  CameraDiagnosticsTestsFixture fixture;
  fixture.SetUp(DiagFixtureOptions{.enable_cros_camera = true});

  // Open camera.
  auto camera_controller = fixture.GetCameraController();
  auto stream = camera_diag::mojom::CameraStream::New();
  stream->width = stream_size.width;
  stream->height = stream_size.height;
  stream->pixel_format = camera_diag::mojom::PixelFormat::kYuv420;
  camera_controller->OpenCamera(std::move(stream), FrameType::kGreen);

  base::ElapsedTimer timer;

  auto result = fixture.RunFrameAnalysis(kFrameAnalysisDurationMsDefault);

  int64_t time_spent_ms = timer.Elapsed().InMilliseconds();
  ASSERT_LE(time_spent_ms, kFrameAnalysisDurationMsDefault);

  ASSERT_FALSE(result.is_null());
  ASSERT_TRUE(result->is_res());
  ASSERT_EQ(result->get_res()->suggested_issue,
            cros::camera_diag::mojom::CameraIssue::kNone);
}

}  // namespace cros::tests

int main(int argc, char** argv) {
#if USE_DLC
  cros::DlcLoader client(cros::dlc_client::kBlurDetectorDlcId);
  client.Run();
  if (!client.DlcLoaded()) {
    LOG(ERROR) << "Failed to load DLC";
    return -1;
  }
  base::FilePath dlc_path = client.GetDlcRootPath();
  cros::DlcClient::SetDlcPathForTest(&dlc_path);
#endif  // USE_DLC

  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();

  logging::LoggingSettings settings;
  logging::InitLogging(settings);

  brillo::FlagHelper::Init(argc, argv, "Camera diagnostics tests");

  return RUN_ALL_TESTS();
}
