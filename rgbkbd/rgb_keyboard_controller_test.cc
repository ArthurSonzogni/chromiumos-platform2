// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "rgbkbd/constants.h"
#include "rgbkbd/keyboard_backlight_logger.h"
#include "rgbkbd/rgb_keyboard_controller_impl.h"

#include "base/containers/contains.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"

namespace rgbkbd {
namespace {

const char kPrismUsbSysPath[] = "/sys/prism_path";
const uint8_t kPrismUsbBusNumber = 1;
const uint8_t kPrismDeviceAddress = 2;

// Path to the temporary log file created by `keyboard_backlight_logger`.
const base::FilePath kTempLogFilePath("/tmp/rgbkbd_log");

// TODO(michaelcheco): Move to shared test util.
const std::string CreateSetKeyColorLogEntry(const KeyColor& key_color) {
  return base::StrCat({"RGB::SetKeyColor - ", std::to_string(key_color.key),
                       ",", std::to_string(key_color.color.r), ",",
                       std::to_string(key_color.color.g), ",",
                       std::to_string(key_color.color.b), "\n"});
}

std::string CreateRainbowLogEntry() {
  std::string log;
  for (const auto& key_color : kRainbowModeIndividualKey) {
    log.append(CreateSetKeyColorLogEntry(key_color));
  }
  return log;
}

std::string CreateIndividualKeyZoneColorLogEntry(int zone, Color color) {
  std::string log;
  for (const auto& key : GetIndividualKeyZones()[zone]) {
    KeyColor key_color = {key, color};
    log.append(CreateSetKeyColorLogEntry(key_color));
  }
  return log;
}

const std::string CreateSetAllKeyColorsLogEntry(const Color& color) {
  return base::StrCat({"RGB::SetAllKeyColors - ", std::to_string(color.r), ",",
                       std::to_string(color.g), ",", std::to_string(color.b),
                       "\n"});
}

void ValidateLog(const std::string& expected) {
  std::string file_contents;
  ASSERT_TRUE(base::ReadFileToString(kTempLogFilePath, &file_contents));
  EXPECT_EQ(expected, file_contents);
}

void ValidateLog(base::span<const KeyColor> expected) {
  std::string expected_string;
  for (const auto& key_color : expected) {
    expected_string += CreateSetKeyColorLogEntry(key_color);
  }

  ValidateLog(expected_string);
}

}  // namespace

class RgbKeyboardControllerTest : public testing::Test {
 public:
  RgbKeyboardControllerTest() {
    // Default to RgbKeyboardCapabilities::kIndividualKey
    logger_ = std::make_unique<KeyboardBacklightLogger>(
        kTempLogFilePath, RgbKeyboardCapabilities::kIndividualKey);

    controller_ = std::make_unique<RgbKeyboardControllerImpl>(logger_.get());
  }

  RgbKeyboardControllerTest(const RgbKeyboardControllerTest&) = delete;
  RgbKeyboardControllerTest& operator=(const RgbKeyboardControllerTest&) =
      delete;
  ~RgbKeyboardControllerTest() override = default;

 protected:
  std::unique_ptr<RgbKeyboardControllerImpl> controller_;
  std::unique_ptr<KeyboardBacklightLogger> logger_;
};

TEST_F(RgbKeyboardControllerTest, SetCapabilityIndividualKey) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);

  EXPECT_EQ(static_cast<uint32_t>(RgbKeyboardCapabilities::kIndividualKey),
            controller_->GetRgbKeyboardCapabilities());
}

TEST_F(RgbKeyboardControllerTest, SetCapabilityFourZoneFortyLed) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kFourZoneFortyLed);

  EXPECT_EQ(static_cast<uint32_t>(RgbKeyboardCapabilities::kFourZoneFortyLed),
            controller_->GetRgbKeyboardCapabilities());
}

TEST_F(RgbKeyboardControllerTest, SetCapsLockStateWithDefaultHighlight) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  // Set the background color to something other than |kWhiteBackgroundColor|
  // to ensure the default caps lock highlight color is selected.
  const auto expected_color = Color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);
  EXPECT_TRUE(logger_->ResetLog());
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());
  const std::vector<KeyColor> caps_lock_colors = {
      {kLeftShiftKey, kCapsLockHighlightDefault},
      {kRightShiftKey, kCapsLockHighlightDefault}};
  ValidateLog(std::move(caps_lock_colors));

  // Disable caps lock and verify that the background color is restored.
  EXPECT_TRUE(logger_->ResetLog());
  controller_->SetCapsLockState(/*enabled=*/false);
  const std::vector<KeyColor> default_colors = {
      {kLeftShiftKey, expected_color}, {kRightShiftKey, expected_color}};

  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  ValidateLog(std::move(default_colors));
}

TEST_F(RgbKeyboardControllerTest, SetCapsLockStateWithAlternateHighlight) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  EXPECT_TRUE(logger_->ResetLog());
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());
  // Background color defaults to kWhiteBackgroundColor so expect the alternate
  // caps lock highlight color to be used.
  const std::vector<KeyColor> caps_lock_colors = {
      {kLeftShiftKey, kCapsLockHighlightAlternate},
      {kRightShiftKey, kCapsLockHighlightAlternate}};
  ValidateLog(std::move(caps_lock_colors));
}

TEST_F(RgbKeyboardControllerTest, SetRainbowModeFourZoneFortyLed) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kFourZoneFortyLed);
  controller_->SetRainbowMode();
  ValidateLog(kRainbowModeFourZoneFortyLed);
}

TEST_F(RgbKeyboardControllerTest, SetRainbowModeIndividualKey) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  controller_->SetRainbowMode();
  ValidateLog(kRainbowModeIndividualKey);
}

TEST_F(RgbKeyboardControllerTest, SetRainbowModeFourZoneTwelveLed) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kFourZoneTwelveLed);
  controller_->SetRainbowMode();
  ValidateLog(kRainbowModeFourZoneTwelveLed);
}

TEST_F(RgbKeyboardControllerTest, SetRainbowModeFourZoneFourLed) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kFourZoneFourLed);
  controller_->SetRainbowMode();
  ValidateLog(kRainbowModeFourZoneFourLed);
}

TEST_F(RgbKeyboardControllerTest, SetRainbowModeWithCapsLock) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Simulate enabling caps lock.
  controller_->SetCapsLockState(/*enabled=*/true);

  // Set rainbow mode.
  controller_->SetRainbowMode();

  ValidateLog(
      controller_->GetRainbowModeColorsWithShiftKeysHighlightedForTesting());
  EXPECT_TRUE(logger_->ResetLog());

  // Disable caps lock.
  controller_->SetCapsLockState(/*enabled=*/false);
  // Since rainbow mode was set, expect disabling caps lock reverts to the
  // correct color
  ValidateLog(CreateSetKeyColorLogEntry({kLeftShiftKey, kRainbowRed}) +
              CreateSetKeyColorLogEntry({kRightShiftKey, kRainbowPurple}));
}

TEST_F(RgbKeyboardControllerTest, SetStaticBackgroundColor) {
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);

  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  const std::string expected_log =
      CreateSetAllKeyColorsLogEntry(expected_color);
  ValidateLog(expected_log);
}

TEST_F(RgbKeyboardControllerTest, SetStaticBackgroundColorWithCapsLock) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Simulate enabling Capslock.
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());

  std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, kCapsLockHighlightAlternate}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, kCapsLockHighlightAlternate});

  ValidateLog(shift_key_logs);
  EXPECT_TRUE(logger_->ResetLog());

  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  // Since Capslock was enabled, it is re-highlighted when the background is
  // set. Capslock is not set to the default highlight color since the
  // background is no longer the default white color.
  shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, kCapsLockHighlightDefault}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, kCapsLockHighlightDefault});
  const std::string background_log =
      CreateSetAllKeyColorsLogEntry(expected_color);
  ValidateLog(background_log + shift_key_logs);
  EXPECT_TRUE(logger_->ResetLog());

  // Disable Capslock.
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());
  controller_->SetCapsLockState(/*enabled=*/false);
  EXPECT_FALSE(controller_->IsCapsLockEnabledForTesting());

  // Since background was set, expect disabling Capslock reverts to the set
  // background color.
  ValidateLog(CreateSetKeyColorLogEntry({kLeftShiftKey, expected_color}) +
              CreateSetKeyColorLogEntry({kRightShiftKey, expected_color}));
}

// TODO(swifton): Add a test with Caps Lock after fixing Caps Lock handling.
TEST_F(RgbKeyboardControllerTest, SetStaticZoneColor) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);

  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  const int expected_zone = 2;

  controller_->SetZoneColor(expected_zone, expected_color.r, expected_color.g,
                            expected_color.b);

  const std::string expected_log =
      CreateIndividualKeyZoneColorLogEntry(expected_zone, expected_color);
  ValidateLog(expected_log);
}

TEST_F(RgbKeyboardControllerTest, SetZoneColorDoesNotPermitOutOfBoundsAccess) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);

  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);

  controller_->SetZoneColor(-1, expected_color.r, expected_color.g,
                            expected_color.b);
  controller_->SetZoneColor(5, expected_color.r, expected_color.g,
                            expected_color.b);

  // The log should be empty.
  ValidateLog("");
}

TEST_F(RgbKeyboardControllerTest, SetCapsLockStateWithPerZoneKeyboard) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kFourZoneFortyLed);

  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);
  const std::string expected_log =
      CreateSetAllKeyColorsLogEntry(expected_color);
  ValidateLog(expected_log);
  EXPECT_TRUE(logger_->ResetLog());

  // Enable caps lock.
  controller_->SetCapsLockState(/*enabled=*/true);

  // Expect the log file to be empty since enabling caps lock should not change
  // the state of the shift keys.
  EXPECT_TRUE(logger_->IsLogEmpty());
}

TEST_F(RgbKeyboardControllerTest, RainbowModeMapUpToDate) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  const auto rainbow_mode_map = controller_->GetRainbowModeMapForTesting();
  EXPECT_EQ(std::size(kRainbowModeIndividualKey), rainbow_mode_map.size());
  for (size_t i = 0; i < std::size(kRainbowModeIndividualKey); i++) {
    EXPECT_TRUE(
        base::Contains(rainbow_mode_map, kRainbowModeIndividualKey[i].key));
  }
}

TEST_F(RgbKeyboardControllerTest, ReinitializeSingleColorCapsLockOn) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  // Simulate enabling Capslock.
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());

  EXPECT_TRUE(logger_->ResetLog());

  controller_->ReinitializeOnDeviceReconnected();

  // Expect the colors to be set to solid color + highlighted Shifts after
  // reinitialization.
  const std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, kCapsLockHighlightDefault}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, kCapsLockHighlightDefault});
  const std::string background_log =
      CreateSetAllKeyColorsLogEntry(expected_color);
  ValidateLog(shift_key_logs + background_log + shift_key_logs);
}

TEST_F(RgbKeyboardControllerTest, ReinitializeSingleColorCapsLockOff) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  EXPECT_TRUE(logger_->ResetLog());

  controller_->ReinitializeOnDeviceReconnected();

  // Expect the colors to be set to solid color + unhighlighted Shifts after
  // reinitialization.
  const std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, expected_color}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, expected_color});
  ValidateLog(shift_key_logs + CreateSetAllKeyColorsLogEntry(expected_color));
}

TEST_F(RgbKeyboardControllerTest,
       ReinitializeWhenColorNotInitializedDoesNothing) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);

  EXPECT_TRUE(logger_->ResetLog());

  controller_->ReinitializeOnDeviceReconnected();
  ValidateLog(std::string());
}

TEST_F(RgbKeyboardControllerTest, ReinitializeRainbowCapsLockOn) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set rainbow mode.
  controller_->SetRainbowMode();

  // Simulate enabling Capslock.
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());

  EXPECT_TRUE(logger_->ResetLog());

  controller_->ReinitializeOnDeviceReconnected();

  // Expect the colors to be set to rainbow + highlighted Shifts after
  // reinitialization.
  ValidateLog(
      controller_->GetRainbowModeColorsWithShiftKeysHighlightedForTesting());
}

TEST_F(RgbKeyboardControllerTest, ReinitializeRainbowCapsLockOff) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set rainbow mode.
  controller_->SetRainbowMode();

  EXPECT_TRUE(logger_->ResetLog());

  controller_->ReinitializeOnDeviceReconnected();

  // Expect the colors to be set to rainbow + unhighlighted Shifts after
  // reinitialization.
  const std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, kRainbowRed}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, kRainbowPurple});
  ValidateLog(shift_key_logs + CreateRainbowLogEntry());
}

TEST_F(RgbKeyboardControllerTest, InitializeWhenPrismUsbConnected) {
  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);
  EXPECT_TRUE(logger_->init_called());
  ValidateLog(std::string());
}

TEST_F(RgbKeyboardControllerTest, ResetWhenPrismUsbRemoved) {
  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);

  controller_->OnUsbDeviceRemoved(kPrismUsbSysPath);
  EXPECT_TRUE(logger_->reset_called());
  ValidateLog(std::string());
}

TEST_F(RgbKeyboardControllerTest, DoNotResetWhenNonPrismUsbRemoved) {
  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);

  controller_->OnUsbDeviceRemoved("/random-path");
  EXPECT_FALSE(logger_->reset_called());
  ValidateLog(std::string());
}

TEST_F(RgbKeyboardControllerTest, DoNotInitializeWhenNonPrismUsbConnected) {
  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, 0x1111, 0x2222);

  EXPECT_FALSE(logger_->init_called());
  ValidateLog(std::string());
}

TEST_F(RgbKeyboardControllerTest, ReinitializeWhenPrismUsbConnected) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  EXPECT_TRUE(logger_->ResetLog());

  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);
  EXPECT_TRUE(logger_->init_called());

  // Expect the colors to be set to solid color + unhighlighted Shifts after
  // reinitialization.
  const std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, expected_color}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, expected_color});
  ValidateLog(shift_key_logs + CreateSetAllKeyColorsLogEntry(expected_color));
}

TEST_F(RgbKeyboardControllerTest,
       ReinitializeRainbowWithCapsLockWhenPrismUsbConnected) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set rainbow mode.
  controller_->SetRainbowMode();

  // Simulate enabling Capslock.
  controller_->SetCapsLockState(/*enabled=*/true);
  EXPECT_TRUE(controller_->IsCapsLockEnabledForTesting());
  EXPECT_TRUE(logger_->ResetLog());

  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);
  EXPECT_TRUE(logger_->init_called());

  // Expect the colors to be set to rainbow + highlighted Shifts after
  // reinitialization.
  ValidateLog(
      controller_->GetRainbowModeColorsWithShiftKeysHighlightedForTesting());
}

TEST_F(RgbKeyboardControllerTest,
       ReinitializeRainbowWithoutCapsLockWhenPrismUsbConnected) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set rainbow mode.
  controller_->SetRainbowMode();
  EXPECT_TRUE(logger_->ResetLog());

  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, kPrismVendorId,
                                kPrismProductId);
  EXPECT_TRUE(logger_->init_called());

  // Expect the colors to be set to rainbow + unhighlighted Shifts after
  // reinitialization.
  const std::string shift_key_logs =
      CreateSetKeyColorLogEntry({kLeftShiftKey, kRainbowRed}) +
      CreateSetKeyColorLogEntry({kRightShiftKey, kRainbowPurple});
  ValidateLog(shift_key_logs + CreateRainbowLogEntry());
}

TEST_F(RgbKeyboardControllerTest, DoNotReinitializeWhenNonPrismUsbConnected) {
  controller_->SetKeyboardCapabilityForTesting(
      RgbKeyboardCapabilities::kIndividualKey);
  // Set static background color.
  const Color expected_color(/*r=*/100, /*g=*/150, /*b=*/200);
  controller_->SetStaticBackgroundColor(expected_color.r, expected_color.g,
                                        expected_color.b);

  EXPECT_TRUE(logger_->ResetLog());

  controller_->OnUsbDeviceAdded(kPrismUsbSysPath, kPrismUsbBusNumber,
                                kPrismDeviceAddress, 0x1111, 0x2222);
  EXPECT_FALSE(logger_->init_called());
  ValidateLog(std::string());
}

}  // namespace rgbkbd
