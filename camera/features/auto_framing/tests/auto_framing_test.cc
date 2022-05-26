/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "camera/features/auto_framing/tests/auto_framing_test_fixture.h"

#include <base/timer/elapsed_timer.h>
#include <brillo/flag_helper.h>

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

#include "cros-camera/camera_buffer_utils.h"

namespace cros::tests {

namespace {

Rect<uint32_t> ToAbsoluteCrop(const Size& size, const Rect<float>& crop) {
  return Rect<uint32_t>(
      static_cast<uint32_t>(static_cast<float>(size.width) * crop.left),
      static_cast<uint32_t>(static_cast<float>(size.height) * crop.top),
      static_cast<uint32_t>(static_cast<float>(size.width) * crop.width),
      static_cast<uint32_t>(static_cast<float>(size.height) * crop.height));
}

}  // namespace

base::FilePath g_test_image_path;
float g_frame_rate = 30.0f;
base::TimeDelta g_duration = base::Seconds(1);
base::TimeDelta g_max_detection_time = base::Seconds(0.5);

// Exercises running the auto-framing pipeline in disabled state.
TEST(AutoFramingTest, Disabled) {
  const Size full_size(1280, 720);
  const Size stream_size(320, 240);
  const base::TimeDelta frame_duration = base::Seconds(1.0f / g_frame_rate);
  const TestFrameInfo frame_info = {
      .duration = g_duration,
      .face_rect =
          ToAbsoluteCrop(full_size, Rect<float>(0.4f, 0.4f, 0.12f, 0.2f)),
  };

  AutoFramingTestFixture fixture;
  ASSERT_TRUE(fixture.LoadTestImage(g_test_image_path));
  ASSERT_TRUE(
      fixture.SetUp(full_size, stream_size, g_frame_rate, {frame_info}));

  base::ElapsedTimer timer;
  base::TimeDelta tick = base::Seconds(0);
  while (tick < frame_info.duration) {
    tick = timer.Elapsed();
    ASSERT_TRUE(
        fixture.ProcessFrame(timer.Elapsed().InNanoseconds(), false, nullptr));
    const base::TimeDelta process_time = timer.Elapsed() - tick;
    ASSERT_LT(process_time, frame_duration);
    base::PlatformThread::Sleep(frame_duration - process_time);
  }
}

// Exercises enabling and disabling auto-framing during streaming.
TEST(AutoFramingTest, DynamicallyEnabled) {
  const Size full_size(1280, 720);
  const Size stream_size(320, 240);
  const base::TimeDelta frame_duration = base::Seconds(1.0f / g_frame_rate);
  const TestFrameInfo frame_info = {
      .duration = g_duration,
      .face_rect =
          ToAbsoluteCrop(full_size, Rect<float>(0.4f, 0.4f, 0.12f, 0.2f)),
  };

  AutoFramingTestFixture fixture;
  ASSERT_TRUE(fixture.LoadTestImage(g_test_image_path));
  ASSERT_TRUE(
      fixture.SetUp(full_size, stream_size, g_frame_rate, {frame_info}));

  base::ElapsedTimer timer;
  base::TimeDelta tick = base::Seconds(0);
  auto is_enabled = [&]() {
    return tick > frame_info.duration / 3 &&
           tick <= frame_info.duration * 2 / 3;
  };
  while (tick < frame_info.duration) {
    tick = timer.Elapsed();
    ASSERT_TRUE(fixture.ProcessFrame(timer.Elapsed().InNanoseconds(),
                                     is_enabled(), nullptr));
    const base::TimeDelta process_time = timer.Elapsed() - tick;
    ASSERT_LT(process_time, frame_duration);
    base::PlatformThread::Sleep(frame_duration - process_time);
  }
}

class AutoFramingTestWithResolutions
    : public ::testing::TestWithParam<std::tuple<Size, Size>> {};

INSTANTIATE_TEST_SUITE_P(
    ,
    AutoFramingTestWithResolutions,
    ::testing::Combine(
        ::testing::Values(Size(1920, 1080), Size(2592, 1944)),
        ::testing::Values(Size(320, 240), Size(1280, 720), Size(1920, 1080))),
    [](const testing::TestParamInfo<std::tuple<Size, Size>>& info) {
      return std::get<0>(info.param).ToString() + "_" +
             std::get<1>(info.param).ToString();
    });

// Exercises continuous framing when the scene contains a face at fixed
// position.
TEST_P(AutoFramingTestWithResolutions, StillFace) {
  const auto& [full_size, stream_size] = GetParam();
  const base::TimeDelta frame_duration = base::Seconds(1.0f / g_frame_rate);
  const TestFrameInfo frame_info = {
      .duration = g_duration,
      .face_rect =
          ToAbsoluteCrop(full_size, Rect<float>(0.3f, 0.45f, 0.06f, 0.1f)),
  };

  AutoFramingTestFixture fixture;
  ASSERT_TRUE(fixture.LoadTestImage(g_test_image_path));
  ASSERT_TRUE(
      fixture.SetUp(full_size, stream_size, g_frame_rate, {frame_info}));

  base::ElapsedTimer timer;
  base::TimeDelta tick = base::Seconds(0);
  bool is_face_detected_ever = false;
  while (tick < frame_info.duration) {
    tick = timer.Elapsed();
    bool is_face_detected = false;
    ASSERT_TRUE(fixture.ProcessFrame(timer.Elapsed().InNanoseconds(), true,
                                     &is_face_detected));
    const base::TimeDelta process_time = timer.Elapsed() - tick;
    ASSERT_LT(process_time, frame_duration);
    is_face_detected_ever |= is_face_detected;
    if (tick + process_time >= g_max_detection_time) {
      EXPECT_TRUE(is_face_detected_ever)
          << "Face not detected in " << g_max_detection_time;
    }
    base::PlatformThread::Sleep(frame_duration - process_time);
  }
}

// Exercises continuous framing when the scene contains a face moving around.
TEST_P(AutoFramingTestWithResolutions, MovingFace) {
  const auto& [full_size, stream_size] = GetParam();
  const base::TimeDelta frame_duration = base::Seconds(1.0f / g_frame_rate);
  const std::vector<TestFrameInfo> frame_infos = {
      {
          .duration = g_duration / 4,
          .face_rect =
              ToAbsoluteCrop(full_size, Rect<float>(0.3f, 0.45f, 0.06f, 0.1f)),
      },
      {
          .duration = g_duration / 4,
          .face_rect =
              ToAbsoluteCrop(full_size, Rect<float>(0.6f, 0.65f, 0.08f, 0.13f)),
      },
      {
          .duration = g_duration / 4,
          .face_rect =
              ToAbsoluteCrop(full_size, Rect<float>(0.5f, 0.65f, 0.09f, 0.15f)),
      },
      {
          .duration = g_duration / 4,
          .face_rect =
              ToAbsoluteCrop(full_size, Rect<float>(0.4f, 0.6f, 0.07f, 0.12f)),
      },
  };

  AutoFramingTestFixture fixture;
  ASSERT_TRUE(fixture.LoadTestImage(g_test_image_path));
  ASSERT_TRUE(fixture.SetUp(full_size, stream_size, g_frame_rate, frame_infos));

  base::ElapsedTimer timer;
  base::TimeDelta tick = base::Seconds(0);
  base::TimeDelta frame_start = base::Seconds(0);
  for (auto& info : frame_infos) {
    bool is_face_detected_ever = false;
    while (tick <= frame_start + info.duration) {
      tick = timer.Elapsed();
      bool is_face_detected = false;
      ASSERT_TRUE(
          fixture.ProcessFrame(tick.InNanoseconds(), true, &is_face_detected));
      const base::TimeDelta process_time = timer.Elapsed() - tick;
      ASSERT_LT(process_time, frame_duration);
      is_face_detected_ever |= is_face_detected;
      if (tick + process_time - frame_start >= g_max_detection_time) {
        EXPECT_TRUE(is_face_detected_ever)
            << "Face not detected in " << g_max_detection_time;
      }
      base::PlatformThread::Sleep(frame_duration - process_time);
    }
    frame_start += info.duration;
  }
}

}  // namespace cros::tests

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);

  logging::LoggingSettings settings;
  logging::InitLogging(settings);

  DEFINE_string(test_image_path, "", "Test image file path");
  DEFINE_double(frame_rate, 30.0, "Frame rate (fps)");
  DEFINE_double(duration, 1.0, "Duration for each test case (seconds)");
  DEFINE_double(max_detection_time, 0.5,
                "Max time for a face to be detected (seconds)");
  brillo::FlagHelper::Init(argc, argv, "Auto-framing pipeline tests");

  LOG_ASSERT(!FLAGS_test_image_path.empty());
  LOG_ASSERT(FLAGS_frame_rate > 0.0);
  LOG_ASSERT(FLAGS_duration > 0.0);
  LOG_ASSERT(FLAGS_max_detection_time > 0.0);
  cros::tests::g_test_image_path = base::FilePath(FLAGS_test_image_path);
  cros::tests::g_frame_rate = static_cast<float>(FLAGS_frame_rate);
  cros::tests::g_duration = base::Seconds(FLAGS_duration);
  cros::tests::g_max_detection_time = base::Seconds(FLAGS_max_detection_time);

  return RUN_ALL_TESTS();
}
