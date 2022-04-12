// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "rgbkbd/keyboard_backlight_logger.h"
#include "rgbkbd/rgb_keyboard_controller_impl.h"

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"

namespace rgbkbd {
namespace {

// Path to the temporary log file created by `keyboard_backlight_logger`.
const base::FilePath kTempLogFilePath("/tmp/rgbkbd_log");

// TODO(michaelcheco): Move to shared test util.
const std::string CreateSetKeyColorLogEntry(uint32_t key, const Color& color) {
  return base::StrCat({"RGB::SetKeyColor - ", std::to_string(key), ",",
                       std::to_string(color.r), ",", std::to_string(color.g),
                       ",", std::to_string(color.b), "\n"});
}

bool ValidateLog(const std::string& expected) {
  std::string file_contents;
  return base::ReadFileToString(kTempLogFilePath, &file_contents) &&
         expected == file_contents;
}

bool ValidateLog(const std::vector<const KeyColor>& expected) {
  std::string expected_string;
  for (const auto& key_color : expected) {
    expected_string +=
        CreateSetKeyColorLogEntry(key_color.key, key_color.color);
  }

  return ValidateLog(expected_string);
}

}  // namespace

class RgbKeyboardControllerTest : public testing::Test {
 public:
  RgbKeyboardControllerTest() {
    auto logger = std::make_unique<KeyboardBacklightLogger>();
    logger_ = logger.get();
    controller_ =
        std::make_unique<RgbKeyboardControllerImpl>(std::move(logger));
  }

  RgbKeyboardControllerTest(const RgbKeyboardControllerTest&) = delete;
  RgbKeyboardControllerTest& operator=(const RgbKeyboardControllerTest&) =
      delete;
  ~RgbKeyboardControllerTest() override = default;

 protected:
  std::unique_ptr<RgbKeyboardControllerImpl> controller_;
  KeyboardBacklightLogger* logger_;
};

// TODO(michaelcheco): Update when we are able to test the real implementation.
TEST_F(RgbKeyboardControllerTest, GetRgbKeyboardCapabilitiesReturnsNone) {
  EXPECT_EQ(controller_->GetRgbKeyboardCapabilities(),
            static_cast<uint32_t>(RgbKeyboardCapabilities::kNone));
}

TEST_F(RgbKeyboardControllerTest, SetCapsLockState) {
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());
  ValidateLog({{kLeftShiftKey, kCapsLockHighlightDefault},
               {kRightShiftKey, kCapsLockHighlightDefault}});

  // Disable caps lock and verify that the background color is restored.
  EXPECT_TRUE(logger_->ResetLog());
  controller_->SetCapsLockState(/*enabled=*/false);
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  ValidateLog({{kLeftShiftKey, kDefaultBackgroundColor},
               {kRightShiftKey, kDefaultBackgroundColor}});
}

}  // namespace rgbkbd
