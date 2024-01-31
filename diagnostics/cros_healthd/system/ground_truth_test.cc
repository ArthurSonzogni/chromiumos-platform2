// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/ground_truth.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/routines/fingerprint/fingerprint.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace cros_config_property = paths::cros_config;
namespace fingerprint = paths::cros_config::fingerprint;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

class GroundTruthTest : public BaseFileTest {
 public:
  GroundTruthTest(const GroundTruthTest&) = delete;
  GroundTruthTest& operator=(const GroundTruthTest&) = delete;

 protected:
  GroundTruthTest() = default;

  MockFlossController* mock_floss_controller() {
    return mock_context_.mock_floss_controller();
  }

  void ExpectEventSupported(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kSupported);
  }

  void ExpectEventUnsupported(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kUnsupported);
  }

  void ExpectEventException(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kException);
  }

  GroundTruth* ground_truth() { return mock_context_.ground_truth(); }

  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;

 private:
  // This makes debugging easier when there is an error in unittest.
  std::string TagToString(const mojom::SupportStatus::Tag tag) {
    switch (tag) {
      case mojom::SupportStatus::Tag::kUnmappedUnionField:
        return "kUnmappedUnionField";
      case mojom::SupportStatus::Tag::kException:
        return "kException";
      case mojom::SupportStatus::Tag::kSupported:
        return "kSupported";
      case mojom::SupportStatus::Tag::kUnsupported:
        return "kUnsupported";
    }
  }

  void ExpectEventStatus(mojom::EventCategoryEnum category,
                         mojom::SupportStatus::Tag expect_status) {
    base::test::TestFuture<mojom::SupportStatusPtr> future;
    mock_context_.ground_truth()->IsEventSupported(category,
                                                   future.GetCallback());
    auto status = future.Take();
    EXPECT_EQ(TagToString(status->which()), TagToString(expect_status));
  }

  MockContext mock_context_;
};

mojom::SupportStatusPtr MakeSupported() {
  return mojom::SupportStatus::NewSupported(mojom::Supported::New());
}

mojom::SupportStatusPtr MakeUnsupported(const std::string& debug_message) {
  return mojom::SupportStatus::NewUnsupported(
      mojom::Unsupported::New(debug_message, /*reason=*/nullptr));
}

mojom::SupportStatusPtr MakeUnexpected(const std::string& debug_message) {
  return mojom::SupportStatus::NewException(mojom::Exception::New(
      mojom::Exception::Reason::kUnexpected, debug_message));
}

TEST_F(GroundTruthTest, AlwaysSupportedEvents) {
  ExpectEventSupported(mojom::EventCategoryEnum::kUsb);
  ExpectEventSupported(mojom::EventCategoryEnum::kThunderbolt);
  ExpectEventSupported(mojom::EventCategoryEnum::kBluetooth);
  ExpectEventSupported(mojom::EventCategoryEnum::kPower);
  ExpectEventSupported(mojom::EventCategoryEnum::kAudio);
  ExpectEventSupported(mojom::EventCategoryEnum::kExternalDisplay);
}

TEST_F(GroundTruthTest, CurrentUnsupported) {
  ExpectEventUnsupported(mojom::EventCategoryEnum::kNetwork);
}

TEST_F(GroundTruthTest, UnmappedField) {
  ExpectEventException(mojom::EventCategoryEnum::kUnmappedEnumField);
}

TEST_F(GroundTruthTest, LidEvent) {
  std::vector<std::pair</*form-factor=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {cros_config_value::kClamshell, true},
          {cros_config_value::kConvertible, true},
          {cros_config_value::kDetachable, true},
          {cros_config_value::kChromebase, false},
          {cros_config_value::kChromebox, false},
          {cros_config_value::kChromebit, false},
          {cros_config_value::kChromeslate, false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kLid);

  for (const auto& [form_factor, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kFormFactor, form_factor);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kLid);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kLid);
    }
  }
}

TEST_F(GroundTruthTest, StylusGarageEvent) {
  std::vector<std::pair</*stylus-category=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {cros_config_value::kStylusCategoryInternal, true},
          {cros_config_value::kStylusCategoryUnknown, false},
          {cros_config_value::kStylusCategoryNone, false},
          {cros_config_value::kStylusCategoryExternal, false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kStylusGarage);

  for (const auto& [stylus_category, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kStylusCategory, stylus_category);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kStylusGarage);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kStylusGarage);
    }
  }
}

TEST_F(GroundTruthTest, StylusEvent) {
  std::vector<std::pair</*stylus-category=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {cros_config_value::kStylusCategoryInternal, true},
          {cros_config_value::kStylusCategoryExternal, true},
          {cros_config_value::kStylusCategoryUnknown, false},
          {cros_config_value::kStylusCategoryNone, false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kStylus);

  for (const auto& [stylus_category, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kStylusCategory, stylus_category);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kStylus);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kStylus);
    }
  }
}

TEST_F(GroundTruthTest, TouchscreenEvent) {
  std::vector<std::pair</*has-touchscreen=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {"true", true},
          {"false", false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kTouchscreen);

  for (const auto& [has_touchscreen, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kHasTouchscreen, has_touchscreen);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kTouchscreen);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kTouchscreen);
    }
  }
}

TEST_F(GroundTruthTest, TouchpadEvent) {
  std::vector<std::pair</*form-factor=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {cros_config_value::kClamshell, true},
          {cros_config_value::kConvertible, true},
          {cros_config_value::kDetachable, true},
          {cros_config_value::kChromebase, false},
          {cros_config_value::kChromebox, false},
          {cros_config_value::kChromebit, false},
          {cros_config_value::kChromeslate, false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kTouchpad);

  for (const auto& [form_factor, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kFormFactor, form_factor);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kTouchpad);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kTouchpad);
    }
  }
}

TEST_F(GroundTruthTest, KeyboardDiagnosticEvent) {
  std::vector<std::pair</*form-factor=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {cros_config_value::kClamshell, true},
          {cros_config_value::kConvertible, true},
          {cros_config_value::kDetachable, true},
          {cros_config_value::kChromebase, false},
          {cros_config_value::kChromebox, false},
          {cros_config_value::kChromebit, false},
          {cros_config_value::kChromeslate, false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kKeyboardDiagnostic);

  for (const auto& [form_factor, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kFormFactor, form_factor);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kKeyboardDiagnostic);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kKeyboardDiagnostic);
    }
  }
}

TEST_F(GroundTruthTest, AudioJackEvent) {
  std::vector<std::pair</*has-audio-jack=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {"true", true},
          {"false", false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kAudioJack);

  for (const auto& [has_audio_jack, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kHasAudioJack, has_audio_jack);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kAudioJack);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kAudioJack);
    }
  }
}

TEST_F(GroundTruthTest, SdCardEvent) {
  std::vector<std::pair</*has-sd-reader=*/std::string, /*supported=*/bool>>
      test_combinations = {
          {"true", true},
          {"false", false},
          {"Others", false},
      };

  // Test not set the cros_config first to simulate file not found.
  ExpectEventUnsupported(mojom::EventCategoryEnum::kSdCard);

  for (const auto& [has_sd_reader, supported] : test_combinations) {
    SetFakeCrosConfig(cros_config_property::kHasSdReader, has_sd_reader);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kSdCard);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kSdCard);
    }
  }
}

TEST_F(GroundTruthTest, PrepareRoutineBatteryCapacity) {
  SetFakeCrosConfig(cros_config_property::kBatteryCapacityLowMah, "123");
  SetFakeCrosConfig(cros_config_property::kBatteryCapacityHighMah, "456");

  std::optional<uint32_t> low_mah;
  std::optional<uint32_t> high_mah;
  EXPECT_EQ(ground_truth()->PrepareRoutineBatteryCapacity(low_mah, high_mah),
            mojom::SupportStatus::NewSupported(mojom::Supported::New()));
  EXPECT_EQ(low_mah, 123);
  EXPECT_EQ(high_mah, 456);
}

TEST_F(GroundTruthTest, PrepareRoutineBatteryHealth) {
  SetFakeCrosConfig(cros_config_property::kBatteryHealthMaximumCycleCount,
                    "123");
  SetFakeCrosConfig(
      cros_config_property::kBatteryHealthPercentBatteryWearAllowed, "45");

  std::optional<uint32_t> maximum_cycle_count;
  std::optional<uint8_t> percent_battery_wear_allowed;
  EXPECT_EQ(ground_truth()->PrepareRoutineBatteryHealth(
                maximum_cycle_count, percent_battery_wear_allowed),
            mojom::SupportStatus::NewSupported(mojom::Supported::New()));
  EXPECT_EQ(maximum_cycle_count, 123);
  EXPECT_EQ(percent_battery_wear_allowed, 45);
}

TEST_F(GroundTruthTest, PrepareRoutinePrimeSearch) {
  SetFakeCrosConfig(cros_config_property::kPrimeSearchMaxNum, "123");

  std::optional<uint64_t> max_num;
  EXPECT_EQ(ground_truth()->PrepareRoutinePrimeSearch(max_num),
            mojom::SupportStatus::NewSupported(mojom::Supported::New()));
  EXPECT_EQ(max_num, 123);
}

TEST_F(GroundTruthTest, PrepareRoutineNvmeWearLevel) {
  SetFakeCrosConfig(cros_config_property::kNvmeWearLevelThreshold, "123");

  std::optional<uint32_t> threshold;
  EXPECT_EQ(ground_truth()->PrepareRoutineNvmeWearLevel(threshold),
            mojom::SupportStatus::NewSupported(mojom::Supported::New()));
  EXPECT_EQ(threshold, 123);
}

TEST_F(GroundTruthTest, PrepareRoutineFingerprint) {
  SetFakeCrosConfig(fingerprint::kMaxDeadPixels, "0");
  SetFakeCrosConfig(fingerprint::kMaxDeadPixelsInDetectZone, "1");
  SetFakeCrosConfig(fingerprint::kMaxPixelDev, "2");
  SetFakeCrosConfig(fingerprint::kMaxErrorResetPixels, "3");
  SetFakeCrosConfig(fingerprint::kMaxResetPixelDev, "4");
  SetFakeCrosConfig(fingerprint::kCbType1Lower, "5");
  SetFakeCrosConfig(fingerprint::kCbType1Upper, "6");
  SetFakeCrosConfig(fingerprint::kCbType2Lower, "7");
  SetFakeCrosConfig(fingerprint::kCbType2Upper, "8");
  SetFakeCrosConfig(fingerprint::kIcbType1Lower, "9");
  SetFakeCrosConfig(fingerprint::kIcbType1Upper, "10");
  SetFakeCrosConfig(fingerprint::kIcbType2Lower, "11");
  SetFakeCrosConfig(fingerprint::kIcbType2Upper, "12");
  SetFakeCrosConfig(fingerprint::kNumDetectZone, "1");
  SetFakeCrosConfig({fingerprint::kDetectZones.ToStr(), "0", fingerprint::kX1},
                    "1");
  SetFakeCrosConfig({fingerprint::kDetectZones.ToStr(), "0", fingerprint::kY1},
                    "2");
  SetFakeCrosConfig({fingerprint::kDetectZones.ToStr(), "0", fingerprint::kX2},
                    "3");
  SetFakeCrosConfig({fingerprint::kDetectZones.ToStr(), "0", fingerprint::kY2},
                    "4");

  FingerprintParameter param;
  EXPECT_EQ(ground_truth()->PrepareRoutineFingerprint(param),
            mojom::SupportStatus::NewSupported(mojom::Supported::New()));
  EXPECT_EQ(param.max_dead_pixels, 0);
  EXPECT_EQ(param.max_dead_pixels_in_detect_zone, 1);
  EXPECT_EQ(param.max_pixel_dev, 2);
  EXPECT_EQ(param.max_error_reset_pixels, 3);
  EXPECT_EQ(param.max_reset_pixel_dev, 4);
  EXPECT_EQ(param.pixel_median.cb_type1_lower, 5);
  EXPECT_EQ(param.pixel_median.cb_type1_upper, 6);
  EXPECT_EQ(param.pixel_median.cb_type2_lower, 7);
  EXPECT_EQ(param.pixel_median.cb_type2_upper, 8);
  EXPECT_EQ(param.pixel_median.icb_type1_lower, 9);
  EXPECT_EQ(param.pixel_median.icb_type1_upper, 10);
  EXPECT_EQ(param.pixel_median.icb_type2_lower, 11);
  EXPECT_EQ(param.pixel_median.icb_type2_upper, 12);
  EXPECT_EQ(param.detect_zones.size(), 1);
  EXPECT_EQ(param.detect_zones[0].x1, 1);
  EXPECT_EQ(param.detect_zones[0].y1, 2);
  EXPECT_EQ(param.detect_zones[0].x2, 3);
  EXPECT_EQ(param.detect_zones[0].y2, 4);
}

TEST_F(GroundTruthTest, PrepareRoutineEmmcLifetime) {
  SetFakeCrosConfig(cros_config_property::kStorageType,
                    cros_config_value::kStorageTypeEmmc);
  SetFile(paths::usr::kMmc, "");

  EXPECT_EQ(ground_truth()->PrepareRoutineEmmcLifetime(), MakeSupported());
}

TEST_F(GroundTruthTest, PrepareRoutineEmmcLifetimeCrosConfigMissingFallback) {
  SetFakeCrosConfig(cros_config_property::kStorageType, std::nullopt);
  SetFile(paths::usr::kMmc, "");

  EXPECT_EQ(ground_truth()->PrepareRoutineEmmcLifetime(), MakeSupported());
}

TEST_F(GroundTruthTest, PrepareRoutineEmmcLifetimeCrosConfigUnknownFallback) {
  SetFakeCrosConfig(cros_config_property::kStorageType, "STORAGE_TYPE_UNKNOWN");
  SetFile(paths::usr::kMmc, "");

  EXPECT_EQ(ground_truth()->PrepareRoutineEmmcLifetime(), MakeSupported());
}

TEST_F(GroundTruthTest, PrepareRoutineEmmcLifetimeUnsupportedNoMmc) {
  SetFakeCrosConfig(cros_config_property::kStorageType,
                    cros_config_value::kStorageTypeEmmc);

  EXPECT_EQ(ground_truth()->PrepareRoutineEmmcLifetime(),
            MakeUnsupported(
                "Not supported on a device without eMMC drive or mmc utility"));
}

TEST_F(GroundTruthTest, PrepareRoutineEmmcLifetimeUnsupportedOtherStorageType) {
  SetFakeCrosConfig(cros_config_property::kStorageType, "UFS");
  SetFile(paths::usr::kMmc, "");

  EXPECT_EQ(ground_truth()->PrepareRoutineEmmcLifetime(),
            MakeUnsupported(
                "Not supported on a device without eMMC drive or mmc utility"));
}

TEST_F(GroundTruthTest, BluetoothRoutineFlossEnabled) {
  EXPECT_CALL(*mock_floss_controller(), GetManager())
      .WillRepeatedly(Return(&mock_manager_proxy_));
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<0>(true));

  base::test::TestFuture<mojom::SupportStatusPtr> future;
  ground_truth()->PrepareRoutineBluetoothFloss(future.GetCallback());
  EXPECT_EQ(future.Get(), MakeSupported());
}

TEST_F(GroundTruthTest, BluetoothRoutineFlossDisabled) {
  EXPECT_CALL(*mock_floss_controller(), GetManager())
      .WillRepeatedly(Return(&mock_manager_proxy_));
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<0>(false));

  base::test::TestFuture<mojom::SupportStatusPtr> future;
  ground_truth()->PrepareRoutineBluetoothFloss(future.GetCallback());
  EXPECT_EQ(future.Get(), MakeUnsupported("Floss is not enabled"));
}

TEST_F(GroundTruthTest, BluetoothRoutineNoFlossManager) {
  EXPECT_CALL(*mock_floss_controller(), GetManager())
      .WillRepeatedly(Return(nullptr));

  base::test::TestFuture<mojom::SupportStatusPtr> future;
  ground_truth()->PrepareRoutineBluetoothFloss(future.GetCallback());
  EXPECT_EQ(future.Get(), MakeUnsupported("Floss is not enabled"));
}

TEST_F(GroundTruthTest, BluetoothRoutineGetFlossEnabledError) {
  EXPECT_CALL(*mock_floss_controller(), GetManager())
      .WillRepeatedly(Return(&mock_manager_proxy_));
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(error.get()));

  base::test::TestFuture<mojom::SupportStatusPtr> future;
  ground_truth()->PrepareRoutineBluetoothFloss(future.GetCallback());
  EXPECT_EQ(future.Get(),
            MakeUnexpected("Got error when checking floss enabled state"));
}

}  // namespace
}  // namespace diagnostics
