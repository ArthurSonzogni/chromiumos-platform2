// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/features/kiosk_vision/kiosk_vision_stream_manipulator.h"

#include <memory>

#include <base/functional/callback_helpers.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <camera/mojo/cros_camera_service.mojom.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "features/kiosk_vision/kiosk_vision_wrapper.h"

namespace cros::tests {

namespace {

const base::FilePath kDlcPath = base::FilePath("test/kiosk/vision/dlc/path");

class FakeObserver : public cros::mojom::KioskVisionObserver {
 public:
  FakeObserver() = default;
  FakeObserver(const FakeObserver&) = delete;
  FakeObserver& operator=(const FakeObserver&) = delete;
  ~FakeObserver() override = default;

  // `cros::mojom::KioskVisionObserver`:
  void OnFrameProcessed(
      cros::mojom::KioskVisionDetectionPtr detection) override {}
  void OnTrackCompleted(cros::mojom::KioskVisionTrackPtr track) override {}
  void OnError(cros::mojom::KioskVisionError error) override {}
};

class FakeKioskVisionWrapper : public KioskVisionWrapper {
 public:
  FakeKioskVisionWrapper(FrameCallback frame_cb,
                         TrackCallback track_cb,
                         ErrorCallback error_cb)
      : KioskVisionWrapper(
            std::move(frame_cb), std::move(track_cb), std::move(error_cb)) {}

  FakeKioskVisionWrapper(const FakeKioskVisionWrapper&) = delete;
  FakeKioskVisionWrapper& operator=(const FakeKioskVisionWrapper&) = delete;
  ~FakeKioskVisionWrapper() override = default;

  InitializeStatus Initialize(const base::FilePath& dlc_root_path) override {
    return InitializeStatus::kOk;
  }

  bool ProcessFrame(int64_t timestamp, buffer_handle_t buffer) override {
    return true;
  }
};

android::CameraMetadata GenerateStaticMetadataFor720p() {
  const std::vector<int32_t> stream_configs = {
      HAL_PIXEL_FORMAT_YCBCR_420_888, 1280, 720,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT};
  const std::vector<int64_t> min_durations = {
      HAL_PIXEL_FORMAT_YCBCR_420_888, 1280, 720,
      static_cast<int64_t>(1e9f / 30.0f)};
  const std::vector<int32_t> active_array_size = {0, 0, 1280, 720};
  const int32_t partial_result_count = 1;

  android::CameraMetadata static_info;
  CHECK_EQ(static_info.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                              std::move(stream_configs)),
           0);
  CHECK_EQ(static_info.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                              std::move(min_durations)),
           0);
  CHECK_EQ(static_info.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
                              std::move(active_array_size)),
           0);
  CHECK_EQ(static_info.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                              &partial_result_count, 1),
           0);

  return static_info;
}

}  // namespace

class KioskVisionStreamManipulatorBaseTest : public ::testing::Test {
 public:
  KioskVisionStreamManipulatorBaseTest() : receiver_(&observer_) {}

  void SetUp() override {
    ::testing::Test::SetUp();
    mojo::core::Init();
    runtime_options_.SetKioskVisionConfig(kDlcPath,
                                          receiver_.BindNewPipeAndPassRemote());

    stream_manipulator_ = std::make_unique<KioskVisionStreamManipulator>(
        &runtime_options_, base::SingleThreadTaskRunner::GetCurrentDefault());
  }

  bool InitializeStreamManipulator(StreamManipulator::CaptureResultCallback
                                       result_callback = base::DoNothing()) {
    static_info_ = GenerateStaticMetadataFor720p();

    return stream_manipulator_->Initialize(
        static_info_.getAndLock(),
        StreamManipulator::Callbacks{.result_callback = result_callback,
                                     .notify_callback = base::DoNothing()});
  }

 protected:
  std::unique_ptr<KioskVisionStreamManipulator> stream_manipulator_;
  base::test::TaskEnvironment task_environment_;

  FakeObserver observer_;
  mojo::Receiver<cros::mojom::KioskVisionObserver> receiver_;
  StreamManipulator::RuntimeOptions runtime_options_;
  android::CameraMetadata static_info_;
};

TEST_F(KioskVisionStreamManipulatorBaseTest, Create) {
  CHECK_EQ(stream_manipulator_->GetStatusForTesting(),
           KioskVisionStreamManipulator::Status::kNotInitialized);
  CHECK_EQ(stream_manipulator_->GetDlcPathForTesting(), kDlcPath);
}

TEST_F(KioskVisionStreamManipulatorBaseTest, InitializeNoDlc) {
  EXPECT_FALSE(InitializeStreamManipulator());

  CHECK_EQ(stream_manipulator_->GetStatusForTesting(),
           KioskVisionStreamManipulator::Status::kDlcError);
}

// This test class uses `FakeKioskVisionWrapper` to avoid setting up the DLC
// which is unavailable in unit tests.
class KioskVisionStreamManipulatorTest
    : public KioskVisionStreamManipulatorBaseTest {
 public:
  KioskVisionStreamManipulatorTest() = default;

  void SetUp() override {
    ::testing::Test::SetUp();
    mojo::core::Init();
    runtime_options_.SetKioskVisionConfig(kDlcPath,
                                          receiver_.BindNewPipeAndPassRemote());

    std::unique_ptr<FakeKioskVisionWrapper> wrapper =
        std::make_unique<FakeKioskVisionWrapper>(
            base::BindRepeating(
                &KioskVisionStreamManipulatorTest::OnFrameProcessed,
                base::Unretained(this)),
            base::BindRepeating(
                &KioskVisionStreamManipulatorTest::OnTrackCompleted,
                base::Unretained(this)),
            base::BindRepeating(&KioskVisionStreamManipulatorTest::OnError,
                                base::Unretained(this)));

    stream_manipulator_ = std::make_unique<KioskVisionStreamManipulator>(
        &runtime_options_, base::SingleThreadTaskRunner::GetCurrentDefault(),
        std::move(wrapper));
  }

 protected:
  void OnFrameProcessed(cros::kiosk_vision::Timestamp timestamp,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size) {
    stream_manipulator_->OnFrameProcessed(timestamp, audience_data,
                                          audience_size);
  }

  void OnTrackCompleted(cros::kiosk_vision::TrackID id,
                        const cros::kiosk_vision::Appearance* appearances_data,
                        uint32_t appearances_size,
                        cros::kiosk_vision::Timestamp start_time,
                        cros::kiosk_vision::Timestamp end_time) {
    stream_manipulator_->OnTrackCompleted(
        id, appearances_data, appearances_size, start_time, end_time);
  }

  void OnError() { stream_manipulator_->OnError(); }
};

TEST_F(KioskVisionStreamManipulatorTest, Initialize) {
  EXPECT_TRUE(InitializeStreamManipulator());

  CHECK_EQ(stream_manipulator_->GetStatusForTesting(),
           KioskVisionStreamManipulator::Status::kInitialized);
}

// TODO(b/350887890): add tests for the implemented
// `KioskVisionStreamManipulator`.

}  // namespace cros::tests
