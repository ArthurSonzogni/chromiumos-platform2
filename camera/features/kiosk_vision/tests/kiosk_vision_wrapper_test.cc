// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <brillo/flag_helper.h>
#include <gtest/gtest.h>

#include "features/kiosk_vision/kiosk_vision_wrapper.h"
#include "ml_core/dlc/dlc_loader.h"

namespace cros::tests {

constexpr char kKioskVisionDlcId[] = "cros-camera-kiosk-vision-dlc";

base::FilePath g_dlc_path;

class KioskVisionWrapperTest : public ::testing::Test {
 protected:
  bool InitializeWrapper() {
    kiosk_vision_wrapper_ = std::make_unique<KioskVisionWrapper>(
        base::BindRepeating(&KioskVisionWrapperTest::OnFrameProcessed,
                            base::Unretained(this)),
        base::BindRepeating(&KioskVisionWrapperTest::OnTrackCompleted,
                            base::Unretained(this)),
        base::BindRepeating(&KioskVisionWrapperTest::OnError,
                            base::Unretained(this)));
    if (!kiosk_vision_wrapper_->Initialize(g_dlc_path)) {
      LOG(ERROR) << "Failed to initialize KioskVisionWrapper";
      return false;
    }
    return true;
  }

  std::unique_ptr<KioskVisionWrapper> kiosk_vision_wrapper_;

 private:
  void OnFrameProcessed(cros::kiosk_vision::Timestamp timestamp,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size) {}

  void OnTrackCompleted(cros::kiosk_vision::TrackID id,
                        const cros::kiosk_vision::Appearance* appearances_data,
                        uint32_t appearances_size,
                        cros::kiosk_vision::Timestamp start_time,
                        cros::kiosk_vision::Timestamp end_time) {}
  void OnError() {}
};

TEST_F(KioskVisionWrapperTest, FrameCallbackOneInferenceEmpty) {
  ASSERT_TRUE(InitializeWrapper());

  const int32_t input_width = 640;
  const int32_t input_height = 360;
  ScopedBufferHandle empty_buffer = CameraBufferManager::AllocateScopedBuffer(
      input_width, input_height, HAL_PIXEL_FORMAT_YCbCr_420_888, 0);

  ASSERT_TRUE(kiosk_vision_wrapper_->ProcessFrame(0, *empty_buffer));
}

}  // namespace cros::tests

bool SetupDlc(const std::string& dlc_path_override) {
  if (!dlc_path_override.empty()) {
    cros::tests::g_dlc_path = base::FilePath(dlc_path_override);
    return true;
  }

  cros::DlcLoader dlc_client(cros::tests::kKioskVisionDlcId);
  dlc_client.Run();
  if (dlc_client.DlcLoaded()) {
    cros::tests::g_dlc_path = dlc_client.GetDlcRootPath();
    return true;
  }

  return false;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();

  DEFINE_string(dlc_path, "", "DLC path");
  brillo::FlagHelper::Init(argc, argv, "Kiosk Vision unit tests");

  if (!SetupDlc(FLAGS_dlc_path)) {
    LOG(ERROR) << "Failed to load DLC";
    return -1;
  }
  LOG(ERROR) << "dbg. DLC root path: " << cros::tests::g_dlc_path;

  return RUN_ALL_TESTS();
}
