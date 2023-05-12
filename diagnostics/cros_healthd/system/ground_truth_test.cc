// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class GroundTruthTest : public testing::Test {
 protected:
  GroundTruthTest() = default;
  GroundTruthTest(const GroundTruthTest&) = delete;
  GroundTruthTest& operator=(const GroundTruthTest&) = delete;

  void ExpectEventSupported(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kSupported);
  }

  void ExpectEventUnsupported(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kUnsupported);
  }

  void ExpectEventException(mojom::EventCategoryEnum category) {
    ExpectEventStatus(category, mojom::SupportStatus::Tag::kException);
  }

  void SetCrosConfig(const std::string& path,
                     const std::string& property,
                     const std::string& value) {
    mock_context_.fake_cros_config()->SetString(path, property, value);
  }

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
    auto status = ground_truth_.GetEventSupportStatus(category);
    EXPECT_EQ(TagToString(status->which()), TagToString(expect_status));
  }

  MockContext mock_context_;
  GroundTruth ground_truth_{&mock_context_};
};

TEST_F(GroundTruthTest, AlwaysSupported) {
  ExpectEventSupported(mojom::EventCategoryEnum::kUsb);
  ExpectEventSupported(mojom::EventCategoryEnum::kThunderbolt);
  ExpectEventSupported(mojom::EventCategoryEnum::kBluetooth);
  ExpectEventSupported(mojom::EventCategoryEnum::kPower);
  ExpectEventSupported(mojom::EventCategoryEnum::kAudio);
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

  for (const auto& [form_factor, supported] : test_combinations) {
    SetCrosConfig(cros_config_path::kHardwareProperties,
                  cros_config_property::kFormFactor, form_factor);
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
    SetCrosConfig(cros_config_path::kHardwareProperties,
                  cros_config_property::kStylusCategory, stylus_category);
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
    SetCrosConfig(cros_config_path::kHardwareProperties,
                  cros_config_property::kStylusCategory, stylus_category);
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
    SetCrosConfig(cros_config_path::kHardwareProperties,
                  cros_config_property::kHasTouchscreen, has_touchscreen);
    if (supported) {
      ExpectEventSupported(mojom::EventCategoryEnum::kTouchscreen);
    } else {
      ExpectEventUnsupported(mojom::EventCategoryEnum::kTouchscreen);
    }
  }
}

}  // namespace
}  // namespace diagnostics
