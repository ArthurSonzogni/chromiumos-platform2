// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
}  // namespace sane_backend_tests

// Returns true for "y" and false for "n".
static bool _y_or_no() {
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
