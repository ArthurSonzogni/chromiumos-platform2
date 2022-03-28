// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "rgbkbd/rgb_keyboard_controller_impl.h"

namespace rgbkbd {

class RgbKeyboardControllerTest : public testing::Test {
 public:
  RgbKeyboardControllerTest() {
    controller_ = std::make_unique<RgbKeyboardControllerImpl>();
  }

  RgbKeyboardControllerTest(const RgbKeyboardControllerTest&) = delete;
  RgbKeyboardControllerTest& operator=(const RgbKeyboardControllerTest&) =
      delete;
  ~RgbKeyboardControllerTest() override = default;

 protected:
  std::unique_ptr<RgbKeyboardControllerImpl> controller_;
};

// TODO(michaelcheco): Update when we are able to test the real implementation.
TEST_F(RgbKeyboardControllerTest, GetRgbKeyboardCapabilitiesReturnsNone) {
  EXPECT_EQ(controller_->GetRgbKeyboardCapabilities(),
            static_cast<uint32_t>(RgbKeyboardCapabilities::kNone));
}

}  // namespace rgbkbd
