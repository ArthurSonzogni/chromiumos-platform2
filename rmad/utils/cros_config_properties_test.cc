// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_properties.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace rmad {

class CrosConfigPropertiesTest : public testing::Test {
 public:
  CrosConfigPropertiesTest() {}
  base::FilePath GetRootPath() const { return temp_dir_.GetPath(); }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

// Touchscreen.
TEST_F(CrosConfigPropertiesTest, Touchscreen_Yes) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasTouchscreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "true"));

  EXPECT_EQ("Touchscreen:Yes", GetHasTouchscreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Touchscreen_No) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasTouchscreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "false"));

  EXPECT_EQ("Touchscreen:No", GetHasTouchscreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Touchscreen_NotSet) {
  EXPECT_EQ("Touchscreen:No", GetHasTouchscreenDescription(GetRootPath()));
}

// Privacy screen.
TEST_F(CrosConfigPropertiesTest, PrivacyScreen_Yes) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasPrivacyScreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "true"));

  EXPECT_EQ("PrivacyScreen:Yes", GetHasPrivacyScreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, PrivacyScreen_No) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasPrivacyScreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "false"));

  EXPECT_EQ("PrivacyScreen:No", GetHasPrivacyScreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, PrivacyScreen_NotSet) {
  EXPECT_EQ("PrivacyScreen:No", GetHasPrivacyScreenDescription(GetRootPath()));
}

// HDMI.
TEST_F(CrosConfigPropertiesTest, Hdmi_Yes) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasHdmiKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "true"));

  EXPECT_EQ("HDMI:Yes", GetHasHdmiDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Hdmi_No) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasHdmiKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "false"));

  EXPECT_EQ("HDMI:No", GetHasHdmiDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Hdmi_NotSet) {
  EXPECT_EQ("HDMI:No", GetHasHdmiDescription(GetRootPath()));
}

// SD reader.
TEST_F(CrosConfigPropertiesTest, SdReader_Yes) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasSdReaderKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "true"));

  EXPECT_EQ("SDReader:Yes", GetHasSdReaderDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, SdReader_No) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasSdReaderKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "false"));

  EXPECT_EQ("SDReader:No", GetHasSdReaderDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, SdReader_NotSet) {
  EXPECT_EQ("SDReader:No", GetHasSdReaderDescription(GetRootPath()));
}

// Stylus.
TEST_F(CrosConfigPropertiesTest, Stylus_Internal) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesStylusCategoryKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "internal"));

  EXPECT_EQ("Stylus:internal", GetStylusCategoryDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Stylus_Unknown) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesStylusCategoryKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "unknown"));

  EXPECT_EQ("Stylus:N/A", GetStylusCategoryDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Stylus_NotSet) {
  EXPECT_EQ("Stylus:N/A", GetStylusCategoryDescription(GetRootPath()));
}

// Form factor.
TEST_F(CrosConfigPropertiesTest, FormFactor_Clamshell) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesFormFactorKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "CLAMSHELL"));

  EXPECT_EQ("FormFactor:CLAMSHELL", GetFormFactorDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, FormFactor_NotSet) {
  EXPECT_EQ("FormFactor:N/A", GetFormFactorDescription(GetRootPath()));
}

// Storage.
TEST_F(CrosConfigPropertiesTest, Storage_EMMC) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesStorageTypeKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "EMMC"));

  EXPECT_EQ("Storage:EMMC", GetStorageTypeDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Storage_NotSet) {
  EXPECT_EQ("Storage:N/A", GetStorageTypeDescription(GetRootPath()));
}

// Cellular.
TEST_F(CrosConfigPropertiesTest, Cellular_ModelOnly) {
  const base::FilePath property_dir_path = GetRootPath().Append(kCrosModemPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosModemFirmwareVariantKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "model"));

  EXPECT_EQ("Cellular:Yes", GetCellularDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Cellular_ModelAndChip) {
  const base::FilePath property_dir_path = GetRootPath().Append(kCrosModemPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosModemFirmwareVariantKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "model_chip"));

  EXPECT_EQ("Cellular:Yes(chip)", GetCellularDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Cellular_NotSet) {
  EXPECT_EQ("Cellular:No", GetCellularDescription(GetRootPath()));
}

// Fingerprint.
TEST_F(CrosConfigPropertiesTest, Fingerprint_Yes) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosFingerprintPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosFingerprintSensorLocationKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "right-side"));

  EXPECT_EQ("Fingerprint:Yes", GetHasFingerprintDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Fingerprint_No) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosFingerprintPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosFingerprintSensorLocationKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, ""));

  EXPECT_EQ("Fingerprint:No", GetHasFingerprintDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Fingerprint_NotSet) {
  EXPECT_EQ("Fingerprint:No", GetHasFingerprintDescription(GetRootPath()));
}

// Audio.
TEST_F(CrosConfigPropertiesTest, Audio_Set) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosAudioPath).Append(kCrosAudioMainPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosAudioUcmSuffixKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "ucm-suffix"));

  EXPECT_EQ("Audio:ucm-suffix", GetAudioDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, Audio_NotSet) {
  EXPECT_EQ("Audio:N/A", GetAudioDescription(GetRootPath()));
}

// Keyboard backlight.
TEST_F(CrosConfigPropertiesTest, KeyboardBacklight_Yes) {
  const base::FilePath property_dir_path = GetRootPath().Append(kCrosPowerPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosPowerHasKeyboardBacklightKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "1"));

  EXPECT_EQ("KeyboardBacklight:Yes",
            GetHasKeyboardBacklightDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, KeyboardBacklight_No) {
  const base::FilePath property_dir_path = GetRootPath().Append(kCrosPowerPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosPowerHasKeyboardBacklightKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "0"));

  EXPECT_EQ("KeyboardBacklight:No",
            GetHasKeyboardBacklightDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, KeyboardBacklight_NotSet) {
  EXPECT_EQ("KeyboardBacklight:No",
            GetHasKeyboardBacklightDescription(GetRootPath()));
}

// Camera count.
TEST_F(CrosConfigPropertiesTest, CameraCount_Set) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosCameraPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosCameraCountKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "2"));

  EXPECT_EQ("CameraCount:2", GetCameraCountDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, CameraCount_NotSet) {
  EXPECT_EQ("CameraCount:N/A", GetCameraCountDescription(GetRootPath()));
}

// Proximity sensor.
TEST_F(CrosConfigPropertiesTest, ProximitySensor_Set) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosProximitySensor);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));

  EXPECT_EQ("ProximitySensor:Yes",
            GetHasProximitySensorDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, ProximitySensor_NotSet) {
  EXPECT_EQ("ProximitySensor:No",
            GetHasProximitySensorDescription(GetRootPath()));
}

}  // namespace rmad
