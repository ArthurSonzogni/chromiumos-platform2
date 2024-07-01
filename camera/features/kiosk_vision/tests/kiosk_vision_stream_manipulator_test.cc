// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/features/kiosk_vision/kiosk_vision_stream_manipulator.h"

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

}  // namespace

class KioskVisionStreamManipulatorTest : public ::testing::Test {
 public:
  KioskVisionStreamManipulatorTest() : receiver_(&observer_) {}

  void SetUp() override {
    ::testing::Test::SetUp();
    mojo::core::Init();
  }

  void InitialiseStreamManipulator() {
    runtime_options_.SetKioskVisionConfig(kDlcPath,
                                          receiver_.BindNewPipeAndPassRemote());
    stream_manipulator_ =
        std::make_unique<KioskVisionStreamManipulator>(&runtime_options_);
  }

 protected:
  std::unique_ptr<KioskVisionStreamManipulator> stream_manipulator_;
  base::test::TaskEnvironment task_environment_;

  FakeObserver observer_;
  mojo::Receiver<cros::mojom::KioskVisionObserver> receiver_;
  StreamManipulator::RuntimeOptions runtime_options_;
};

TEST_F(KioskVisionStreamManipulatorTest, CreateKioskVisionSM) {
  InitialiseStreamManipulator();

  CHECK_EQ(stream_manipulator_->GetDlcPathForTesting(), kDlcPath);
}

// TODO(b/340801984): add tests for the implemented
// `KioskVisionStreamManipulator`.

}  // namespace cros::tests
