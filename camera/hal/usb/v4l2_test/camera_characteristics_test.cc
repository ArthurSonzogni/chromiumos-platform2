/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "cros-camera/common.h"
#include "hal/usb/camera_characteristics.h"

namespace cros {
namespace {

TEST(CameraCharacteristicsTest, ConfigFileFormat) {
  if (!CameraCharacteristics::ConfigFileExists()) {
    GTEST_SKIP() << "Camera characteristics config file does not exist";
  }
  // This triggers crash when the characteristics file content doesn't follow
  // the format.
  CameraCharacteristics characteristics;
}

}  // namespace
}  // namespace cros

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
