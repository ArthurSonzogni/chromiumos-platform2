// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/features/kiosk_vision/kiosk_vision_stream_manipulator.h"

#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <camera/mojo/cros_camera_service.mojom.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>

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

class KioskVisionStreamManipulatorTest : public ::testing::Test {
 public:
  KioskVisionStreamManipulatorTest() : receiver_(&observer_) {}

  void SetUp() override {
    ::testing::Test::SetUp();
    mojo::core::Init();
    static_info_ = GenerateStaticMetadataFor720p();
    runtime_options_.SetKioskVisionConfig(kDlcPath,
                                          receiver_.BindNewPipeAndPassRemote());

    stream_manipulator_ = std::make_unique<KioskVisionStreamManipulator>(
        &runtime_options_, base::SingleThreadTaskRunner::GetCurrentDefault());
  }

  bool InitializeStreamManipulator() {
    base::RepeatingCallback<void(Camera3CaptureDescriptor)> result_cb =
        base::BindRepeating(
            [](base::WaitableEvent& frame_processed,
               Camera3CaptureDescriptor descriptor) {
              // resume only after the requested frame is processed
              if (descriptor.num_output_buffers() >= 1) {
                frame_processed.Signal();
              }
            },
            std::ref(frame_processed_));

    bool init_result = stream_manipulator_->Initialize(
        static_info_.getAndLock(),
        StreamManipulator::Callbacks{.result_callback = result_cb,
                                     .notify_callback = base::DoNothing()});

    stream_manipulator_->ConfigureStreams(nullptr);
    stream_manipulator_->OnConfiguredStreams(nullptr);

    return init_result;
  }

 protected:
  std::unique_ptr<KioskVisionStreamManipulator> stream_manipulator_;
  base::test::TaskEnvironment task_environment_;

  FakeObserver observer_;
  mojo::Receiver<cros::mojom::KioskVisionObserver> receiver_;
  StreamManipulator::RuntimeOptions runtime_options_;
  android::CameraMetadata static_info_;
  base::WaitableEvent frame_processed_;
};

TEST_F(KioskVisionStreamManipulatorTest, Create) {
  CHECK_EQ(stream_manipulator_->GetStatusForTesting(),
           KioskVisionStreamManipulator::Status::kNotInitialized);
  CHECK_EQ(stream_manipulator_->GetDlcPathForTesting(), kDlcPath);
}

TEST_F(KioskVisionStreamManipulatorTest, InitializeNoDlc) {
  InitializeStreamManipulator();

  CHECK_EQ(stream_manipulator_->GetStatusForTesting(),
           KioskVisionStreamManipulator::Status::kDlcError);
}

// TODO(b/340801984): add tests for the implemented
// `KioskVisionStreamManipulator`.

}  // namespace cros::tests
