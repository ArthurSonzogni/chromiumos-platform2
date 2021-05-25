// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm_error.h"
#include "libhwsec-foundation/error/testing_helper.h"

#include <sstream>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec {
namespace error {

using ::hwsec_foundation::error::CreateError;
using ::hwsec_foundation::error::CreateErrorWrap;
using ::hwsec_foundation::error::ErrorBase;
using ::hwsec_foundation::error::testing::TestForCreateError;
using ::hwsec_foundation::error::testing::TestForCreateErrorWrap;

class TestingTPMErrorTest : public ::testing::Test {
 public:
  TestingTPMErrorTest() {}
  ~TestingTPMErrorTest() override = default;
};

TEST_F(TestingTPMErrorTest, CreateTPMErrorTest) {
  EXPECT_FALSE((TestForCreateError<TPMErrorBase>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPMErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPMErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPMErrorBase, std::string,
                                   TPMRetryAction>::Check::value));

  EXPECT_FALSE((TestForCreateError<TPMError>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPMError, int>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPMError, std::string>::Check::value));
  EXPECT_TRUE((
      TestForCreateError<TPMError, std::string, TPMRetryAction>::Check::value));
  EXPECT_TRUE((TestForCreateError<TPMError, const char[],
                                  TPMRetryAction>::Check::value));
}

TEST_F(TestingTPMErrorTest, TestForCreateErrorWrap) {
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<ErrorBase, ErrorBase, int>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<ErrorBase, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, ErrorBase, std::string,
                                       TPMRetryAction>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPMErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<ErrorBase, TPMErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPMErrorBase,
                                       std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPMErrorBase, std::string,
                                       TPMRetryAction>::Check::value));

  EXPECT_FALSE((TestForCreateErrorWrap<TPMErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMErrorBase, ErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMErrorBase, ErrorBase,
                                       std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMErrorBase, ErrorBase, std::string,
                                       TPMRetryAction>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMErrorBase, TPMErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMErrorBase, TPMErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMErrorBase, TPMErrorBase,
                                       std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMErrorBase, TPMErrorBase, std::string,
                                       TPMRetryAction>::Check::value));

  EXPECT_FALSE((TestForCreateErrorWrap<TPMError, ErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMError, ErrorBase, int>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMError, ErrorBase, std::string>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, ErrorBase, std::string,
                                      TPMRetryAction>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, ErrorBase, const char[],
                                      TPMRetryAction>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMError, TPMErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMError, TPMErrorBase, int>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPMErrorBase,
                                      std::string>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPMErrorBase,
                                      const char[]>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPMErrorBase, std::string,
                                      TPMRetryAction>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPMErrorBase, const char[],
                                      TPMRetryAction>::Check::value));
}

TEST_F(TestingTPMErrorTest, TPMRetryAction) {
  auto err = CreateError<TPMError>("OuOb", TPMRetryAction::kReboot);
  EXPECT_EQ(err->ToTPMRetryAction(), TPMRetryAction::kReboot);
  auto err2 = CreateErrorWrap<TPMError>(std::move(err), "OuQ");
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("OuQ: OuOb", ss.str());
  EXPECT_EQ(err2->ToTPMRetryAction(), TPMRetryAction::kReboot);
}

}  // namespace error
}  // namespace hwsec
