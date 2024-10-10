// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <set>
#include <string>

#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

#include "lorgnette/sane_option.h"

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
namespace {

// Returns true for "y" and false for "n".
bool _y_or_no() {
  do {
    std::string answer;
    std::getline(std::cin, answer);
    answer = base::ToLowerASCII(answer);
    if (answer == "y") {
      return true;
    } else if (answer == "n") {
      return false;
    } else {
      std::cout << "Please answer \"y\" or \"n\"" << "\n";
    }
  } while (true);
}

class OptionDescriptorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(
        sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_),
        SANE_STATUS_GOOD)
        << "Failed to open scanner";
  }

  void TearDown() override { sane_close(handle_); }

  std::unique_ptr<lorgnette::SaneOption> GetSourceOption() {
    std::unique_ptr<lorgnette::SaneOption> option = nullptr;

    // Index 0 is the well-known option 0, which we skip here.
    SANE_Int i = 1;
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, i);
    while (descriptor) {
      if (!descriptor->name ||
          strcmp(descriptor->name, SANE_NAME_SCAN_SOURCE) != 0) {
        i++;
        descriptor = sane_get_option_descriptor(handle_, i);
        continue;
      }

      option = std::make_unique<lorgnette::SaneOption>(*descriptor, i);
      break;
    }

    return option;
  }

  void TestResolution() {
    bool resolution_found = false;

    // Index 0 is the well-known option 0, which we skip here.
    SANE_Int i = 1;
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, i);
    while (descriptor) {
      if (!descriptor->name ||
          strcmp(descriptor->name, SANE_NAME_SCAN_RESOLUTION) != 0) {
        i++;
        descriptor = sane_get_option_descriptor(handle_, i);
        continue;
      }

      EXPECT_EQ(descriptor->unit, SANE_UNIT_DPI)
          << "Resolution option does not have unit: DPI";

      EXPECT_TRUE(descriptor->type == SANE_TYPE_INT ||
                  descriptor->type == SANE_TYPE_FIXED)
          << "Resolution option has invalid type: " << descriptor->type;

      EXPECT_TRUE(descriptor->constraint_type == SANE_CONSTRAINT_RANGE ||
                  descriptor->constraint_type == SANE_CONSTRAINT_WORD_LIST)
          << "Resolution option has invalid constraint type: "
          << descriptor->constraint_type;

      bool supported_resolution_found = false;
      const std::set<uint32_t> supported_resolutions = {100, 150, 200, 300,
                                                        600};
      lorgnette::SaneOption option(*descriptor, i);
      auto maybe_values = option.GetValidIntValues();
      ASSERT_TRUE(maybe_values) << "Unable to parse resolution option";

      for (auto resolution : maybe_values.value()) {
        if (supported_resolutions.contains(resolution)) {
          supported_resolution_found = true;
          break;
        }
      }

      EXPECT_TRUE(supported_resolution_found)
          << "No supported resolutions found";

      resolution_found = true;
      break;
    }

    EXPECT_TRUE(resolution_found)
        << "Required option missing for name: resolution";
  }

  void TestColorMode() {
    bool color_mode_found = false;

    // Index 0 is the well-known option 0, which we skip here.
    SANE_Int i = 1;
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, i);
    while (descriptor) {
      if (!descriptor->name ||
          strcmp(descriptor->name, SANE_NAME_SCAN_MODE) != 0) {
        i++;
        descriptor = sane_get_option_descriptor(handle_, i);
        continue;
      }

      EXPECT_EQ(descriptor->type, SANE_TYPE_STRING)
          << "Color mode option does not have type: string";

      EXPECT_EQ(descriptor->constraint_type, SANE_CONSTRAINT_STRING_LIST)
          << "Color mode option does not have constraint type: string list";

      bool supported_color_mode_found = false;
      const std::set<std::string> supported_color_modes = {"Lineart", "Gray",
                                                           "Color"};
      lorgnette::SaneOption option(*descriptor, i);
      auto maybe_values = option.GetValidStringValues();
      ASSERT_TRUE(maybe_values) << "Unable to parse color mode option";

      for (auto mode : maybe_values.value()) {
        if (supported_color_modes.contains(mode)) {
          supported_color_mode_found = true;
        }
      }

      EXPECT_TRUE(supported_color_mode_found)
          << "No supported color modes found";

      color_mode_found = true;
      break;
    }

    EXPECT_TRUE(color_mode_found) << "Required option missing for name: mode";
  }

  void TestColorDepth() {
    bool color_depth_found = false;

    // Index 0 is the well-known option 0, which we skip here.
    SANE_Int i = 1;
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, i);
    while (descriptor) {
      if (!descriptor->name ||
          strcmp(descriptor->name, SANE_NAME_BIT_DEPTH) != 0) {
        i++;
        descriptor = sane_get_option_descriptor(handle_, i);
        continue;
      }

      EXPECT_EQ(descriptor->unit, SANE_UNIT_BIT)
          << "Color depth option does not have unit: bit";

      EXPECT_EQ(descriptor->type, SANE_TYPE_INT)
          << "Color depth option does not have type: int";

      EXPECT_EQ(descriptor->constraint_type, SANE_CONSTRAINT_WORD_LIST)
          << "Color depth option does not have constraint type: word list";

      color_depth_found = true;
      break;
    }

    EXPECT_TRUE(color_depth_found) << "Required option missing for name: depth";
  }

  void TestADFJustification(bool& adf_justification_found) {
    // Index 0 is the well-known option 0, which we skip here.
    SANE_Int i = 1;
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, i);
    while (descriptor) {
      if (!descriptor->name ||
          strcmp(descriptor->name, "adf-justification-x") != 0) {
        i++;
        descriptor = sane_get_option_descriptor(handle_, i);
        continue;
      }

      EXPECT_EQ(descriptor->type, SANE_TYPE_STRING)
          << "ADF justification option does not have type: string";

      EXPECT_EQ(descriptor->constraint_type, SANE_CONSTRAINT_STRING_LIST)
          << "ADF justification option does not have constraint type: string "
             "list";

      lorgnette::SaneOption option(*descriptor, i);
      auto maybe_values = option.GetValidStringValues();
      ASSERT_TRUE(maybe_values) << "Unable to parse ADF justification option";
      EXPECT_THAT(maybe_values.value(), ::testing::UnorderedElementsAreArray(
                                            {"left", "center", "right"}));

      adf_justification_found = true;
      break;
    }
  }

  SANE_Handle handle_;
};

TEST_F(OptionDescriptorTest, VerifyOption0) {
  SANE_Int option0_value;
  ASSERT_EQ(sane_control_option(handle_, 0, SANE_ACTION_GET_VALUE,
                                &option0_value, NULL),
            SANE_STATUS_GOOD)
      << "Failed to retrieve option 0";

  EXPECT_GT(option0_value, 0);
}

TEST_F(OptionDescriptorTest, ScanSource) {
  std::cout << "Does the scanner have multiple sources (y/n):" << std::endl;
  bool source_required = _y_or_no();
  bool source_found = false;

  // Index 0 is the well-known option 0, which we skip here.
  SANE_Int i = 1;
  const SANE_Option_Descriptor* descriptor =
      sane_get_option_descriptor(handle_, i);
  while (descriptor) {
    if (!descriptor->name ||
        strcmp(descriptor->name, SANE_NAME_SCAN_SOURCE) != 0) {
      i++;
      descriptor = sane_get_option_descriptor(handle_, i);
      continue;
    }

    EXPECT_EQ(descriptor->type, SANE_TYPE_STRING)
        << "Source option does not have type: string";

    ASSERT_EQ(descriptor->constraint_type, SANE_CONSTRAINT_STRING_LIST)
        << "Source option does not have constraint type: string list";

    ASSERT_TRUE(descriptor->constraint.string_list)
        << "Source option does not have a valid constraint";

    int num_sources = 0;
    while (descriptor->constraint.string_list[num_sources]) {
      num_sources++;
    }

    if (source_required) {
      EXPECT_GT(num_sources, 1)
          << "Multi-source scanner reports too few sources";
    } else {
      EXPECT_GT(num_sources, 0)
          << "Source option does not have any allowed values";
    }

    source_found = true;
    break;
  }

  if (source_required) {
    EXPECT_TRUE(source_found) << "Required option missing for name: source";
  }
}

TEST_F(OptionDescriptorTest, Resolution) {
  auto option = GetSourceOption();

  if (!option) {
    // The scanner did not provide a source option. It must only have a single
    // source.
    TestResolution();
  } else {
    std::optional<std::vector<std::string>> sources =
        option->GetValidStringValues();
    ASSERT_TRUE(sources.has_value());
    for (auto source : *sources) {
      option->Set(source);
      ASSERT_EQ(SANE_STATUS_GOOD,
                sane_control_option(handle_, option->GetIndex(),
                                    SANE_ACTION_SET_VALUE, option->GetPointer(),
                                    nullptr));
      TestResolution();
    }
  }
}

TEST_F(OptionDescriptorTest, ColorMode) {
  auto option = GetSourceOption();

  if (!option) {
    // The scanner did not provide a source option. It must only have a single
    // source.
    TestColorMode();
  } else {
    std::optional<std::vector<std::string>> sources =
        option->GetValidStringValues();
    ASSERT_TRUE(sources.has_value());
    for (auto source : *sources) {
      option->Set(source);
      ASSERT_EQ(SANE_STATUS_GOOD,
                sane_control_option(handle_, option->GetIndex(),
                                    SANE_ACTION_SET_VALUE, option->GetPointer(),
                                    nullptr));
      TestColorMode();
    }
  }
}

TEST_F(OptionDescriptorTest, ColorDepth) {
  std::cout << "Does the scanner advertise multiple depths for a given color "
               "mode? (y/n):"
            << std::endl;
  if (!_y_or_no()) {
    GTEST_SKIP() << "Scanner does not advertise multiple color depths";
  }

  auto option = GetSourceOption();

  if (!option) {
    // The scanner did not provide a source option. It must only have a single
    // source.
    TestColorDepth();
  } else {
    std::optional<std::vector<std::string>> sources =
        option->GetValidStringValues();
    ASSERT_TRUE(sources.has_value());
    for (auto source : *sources) {
      option->Set(source);
      ASSERT_EQ(SANE_STATUS_GOOD,
                sane_control_option(handle_, option->GetIndex(),
                                    SANE_ACTION_SET_VALUE, option->GetPointer(),
                                    nullptr));
      TestColorDepth();
    }
  }
}

TEST_F(OptionDescriptorTest, ADFJustification) {
  std::cout << "Does the scanner have an ADF? (y/n):" << std::endl;
  if (!_y_or_no()) {
    GTEST_SKIP() << "Scanner does not have an ADF";
  }

  bool adf_justification_found = false;
  auto option = GetSourceOption();

  if (!option) {
    // The scanner did not provide a source option. It must only have a single
    // source.
    TestADFJustification(adf_justification_found);
  } else {
    std::optional<std::vector<std::string>> sources =
        option->GetValidStringValues();
    ASSERT_TRUE(sources.has_value());
    for (auto source : *sources) {
      option->Set(source);
      ASSERT_EQ(SANE_STATUS_GOOD,
                sane_control_option(handle_, option->GetIndex(),
                                    SANE_ACTION_SET_VALUE, option->GetPointer(),
                                    nullptr));
      TestADFJustification(adf_justification_found);
    }
  }

  if (!adf_justification_found) {
    GTEST_SKIP() << "ADF Justification not found and is not required";
  }
}
}  // namespace
}  // namespace sane_backend_tests
