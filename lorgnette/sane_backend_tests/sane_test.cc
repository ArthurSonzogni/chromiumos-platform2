// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdlib>
#include <optional>
#include <string>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>
#include <sane/sane.h>
#include <sane/saneopts.h>

#include "base/logging.h"

// SANETest - Try to only use the SANE API directly ///////////////////////////

namespace sane_backend_tests {
// Declared by GoogleTest main wrapper.
extern const std::string* scanner_under_test;
namespace {

class SANETest : public testing::Test {
  void SetUp() override {
    SANE_Int _ignored;
    // This is safe because duplicate sane_init calls one after another are
    // safe.
    ASSERT_EQ(sane_init(&_ignored, nullptr), SANE_STATUS_GOOD);
  }

  void TearDown() override {
    // This is safe because duplicate sane_exit calls one after another are
    // safe.
    sane_exit();
  }
};

// Returns null if no alternative scanner can be found.
std::optional<std::string> _get_scanner_for_multiple_device_open_test() {
  std::cout << "Choose a scanner different from "
            << *sane_backend_tests::scanner_under_test
            << " for validating the backend under test" << "\n";

  std::cout << "Press enter when another scanner is connected to the DUT"
            << "\n";
  std::string ignored;
  std::getline(std::cin, ignored);

  const SANE_Device** devs;
  SANE_Status status = sane_get_devices(&devs, SANE_TRUE);

  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "sane_get_devices() returned status " << status;
    return std::nullopt;
  } else if (devs == nullptr) {
    LOG(ERROR) << "Failed to retrieve devices from sane_get_devices()";
    return std::nullopt;
  }

  std::vector<std::string> scanner_choices;
  for (int i = 0; devs[i]; i++) {
    std::string dev_name = devs[i]->name;

    // We want to choose a different scanner than the one we are usually testing
    // with.
    if (dev_name == *sane_backend_tests::scanner_under_test) {
      continue;
    }
    scanner_choices.push_back(dev_name);
  }

  if (scanner_choices.empty()) {
    LOG(ERROR) << "Failed to find an alternative scanner to pick. Is there "
                  "another scanner plugged in?";
    return std::nullopt;
  }

  for (int i = 0; i < scanner_choices.size(); i++) {
    std::cout << "[" << i << "]: " << scanner_choices[i] << "\n";
  }

  while (true) {
    int parsed_choice;
    std::cout << "Pick an option or press enter for [0]: ";
    std::string raw_option;
    std::getline(std::cin, raw_option);
    parsed_choice = std::atoi(raw_option.c_str());

    if (parsed_choice >= 0 && parsed_choice < scanner_choices.size()) {
      return scanner_choices[parsed_choice];
    }
    std::cout << "Please select an option from the given choices..." << "\n";
  }
}

TEST_F(SANETest, TwoDeviceOpen) {
  if (base::StartsWith(
          base::ToLowerASCII(*sane_backend_tests::scanner_under_test),
          "pfufs")) {
    GTEST_SKIP() << "See b/365111847: pfufs backend skip of "
                    "SANETest.TwoDeviceOpen";
  }

  SANE_Handle handle_1;
  SANE_Handle handle_2;

  // Accept second scanner from tester
  std::optional<std::string> alt_scanner_opt =
      _get_scanner_for_multiple_device_open_test();
  ASSERT_TRUE(alt_scanner_opt.has_value())
      << "Could not get alternative scanner from tester";
  std::cout << "Using " << alt_scanner_opt.value()
            << " as alternative scanner for test." << "\n";
  std::string alt_scanner = alt_scanner_opt.value();

  ASSERT_EQ(
      sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_1),
      SANE_STATUS_GOOD)
      << "Failed to open scanner " << *sane_backend_tests::scanner_under_test;
  ASSERT_EQ(sane_open(alt_scanner.c_str(), &handle_2), SANE_STATUS_GOOD)
      << "Failed to open scanner " << alt_scanner;

  sane_close(handle_1);
  sane_close(handle_2);

  // We run sane_open/close a second time because it found an error where
  // opening the first scanner a second time after closing the second scanner
  // results in a SANE_STATUS_DEVICE_BUSY when reopening the first scanner
  // again.

  ASSERT_EQ(
      sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_1),
      SANE_STATUS_GOOD)
      << "Failed to open scanner " << *sane_backend_tests::scanner_under_test;
  ASSERT_EQ(sane_open(alt_scanner.c_str(), &handle_2), SANE_STATUS_GOOD)
      << "Failed to open scanner " << alt_scanner;

  sane_close(handle_1);
  sane_close(handle_2);
}

TEST_F(SANETest, OpenExitStress) {
  if (base::StartsWith(
          base::ToLowerASCII(*sane_backend_tests::scanner_under_test),
          "pfufs")) {
    GTEST_SKIP() << "See b/365771471: pfufs backend skip of "
                    "SANETest.OpenExitStress";
  }

  SANE_Handle handle_;

  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(sane_init(nullptr, nullptr), SANE_STATUS_GOOD);
    ASSERT_EQ(
        sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_),
        SANE_STATUS_GOOD)
        << "Failed to open scanner on iteration " << i;
    sane_exit();
  }
}

TEST_F(SANETest, OpenCloseStress) {
  SANE_Handle handle_;

  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(
        sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle_),
        SANE_STATUS_GOOD)
        << "Failed to open scanner on iteration " << i;
    sane_close(handle_);
  }
}

TEST_F(SANETest, MultipleCancel) {
  SANE_Handle handle;

  ASSERT_EQ(sane_open(sane_backend_tests::scanner_under_test->c_str(), &handle),
            SANE_STATUS_GOOD)
      << "Failed to open scanner";

  ASSERT_EQ(sane_start(handle), SANE_STATUS_GOOD) << "Failed to start scan";

  std::cout << "Canceling scan\n";
  sane_cancel(handle);
  sane_cancel(handle);

  std::cout << "Press enter when ready to scan again";
  std::string ignored;
  std::getline(std::cin, ignored);

  ASSERT_EQ(sane_start(handle), SANE_STATUS_GOOD)
      << "Failed to restart scan after canceling";

  sane_close(handle);
}

}  // namespace
}  // namespace sane_backend_tests
