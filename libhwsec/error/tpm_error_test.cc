// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec-foundation/error/testing_helper.h"

#include <sstream>
#include <type_traits>

#include <base/test/task_environment.h>
#include <base/test/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec {
namespace error {

using ::hwsec_foundation::error::CreateError;
using ::hwsec_foundation::error::ErrorBase;
using ::hwsec_foundation::error::WrapError;
using ::hwsec_foundation::error::testing::TestForCreateError;
using ::hwsec_foundation::error::testing::TestForWrapError;

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

TEST_F(TestingTPMErrorTest, TestForWrapError) {
  EXPECT_FALSE((TestForWrapError<ErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE((TestForWrapError<ErrorBase, ErrorBase, int>::Check::value));
  EXPECT_FALSE(
      (TestForWrapError<ErrorBase, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForWrapError<ErrorBase, ErrorBase, std::string,
                                 TPMRetryAction>::Check::value));
  EXPECT_FALSE((TestForWrapError<ErrorBase, TPMErrorBase>::Check::value));
  EXPECT_FALSE((TestForWrapError<ErrorBase, TPMErrorBase, int>::Check::value));
  EXPECT_FALSE(
      (TestForWrapError<ErrorBase, TPMErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForWrapError<ErrorBase, TPMErrorBase, std::string,
                                 TPMRetryAction>::Check::value));

  EXPECT_FALSE((TestForWrapError<TPMErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMErrorBase, ErrorBase, int>::Check::value));
  EXPECT_FALSE(
      (TestForWrapError<TPMErrorBase, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMErrorBase, ErrorBase, std::string,
                                 TPMRetryAction>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMErrorBase, TPMErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForWrapError<TPMErrorBase, TPMErrorBase, int>::Check::value));
  EXPECT_FALSE((
      TestForWrapError<TPMErrorBase, TPMErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMErrorBase, TPMErrorBase, std::string,
                                 TPMRetryAction>::Check::value));

  EXPECT_FALSE((TestForWrapError<TPMError, ErrorBase>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMError, ErrorBase, int>::Check::value));
  EXPECT_FALSE(
      (TestForWrapError<TPMError, ErrorBase, std::string>::Check::value));
  EXPECT_TRUE((TestForWrapError<TPMError, ErrorBase, std::string,
                                TPMRetryAction>::Check::value));
  EXPECT_TRUE((TestForWrapError<TPMError, ErrorBase, const char[],
                                TPMRetryAction>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMError, TPMErrorBase>::Check::value));
  EXPECT_FALSE((TestForWrapError<TPMError, TPMErrorBase, int>::Check::value));
  EXPECT_TRUE(
      (TestForWrapError<TPMError, TPMErrorBase, std::string>::Check::value));
  EXPECT_TRUE(
      (TestForWrapError<TPMError, TPMErrorBase, const char[]>::Check::value));
  EXPECT_TRUE((TestForWrapError<TPMError, TPMErrorBase, std::string,
                                TPMRetryAction>::Check::value));
  EXPECT_TRUE((TestForWrapError<TPMError, TPMErrorBase, const char[],
                                TPMRetryAction>::Check::value));
}

TEST_F(TestingTPMErrorTest, TPMRetryAction) {
  auto err = CreateError<TPMError>("OuOb", TPMRetryAction::kReboot);
  EXPECT_EQ(err->ToTPMRetryAction(), TPMRetryAction::kReboot);
  auto err2 = WrapError<TPMError>(std::move(err), "OuQ");
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("OuQ: OuOb", ss.str());
  EXPECT_EQ(err2->ToTPMRetryAction(), TPMRetryAction::kReboot);
}

TEST_F(TestingTPMErrorTest, TPMRetryHandler) {
  auto err = HANDLE_TPM_COMM_ERROR(
      CreateError<TPMError>("OuOb", TPMRetryAction::kReboot));
  EXPECT_EQ("OuOb", err->ToFullReadableString());
  EXPECT_EQ(TPMRetryAction::kReboot, err->ToTPMRetryAction());

  int counter = 0;
  auto func = base::BindLambdaForTesting([&]() {
    counter++;
    return CreateError<TPMError>("OwO", TPMRetryAction::kCommunication);
  });

  auto err2 = HANDLE_TPM_COMM_ERROR(func.Run());
  EXPECT_EQ("Retry Failed: OwO", err2->ToFullReadableString());
  EXPECT_EQ(TPMRetryAction::kLater, err2->ToTPMRetryAction());
  EXPECT_EQ(counter, 5);
}

}  // namespace error
}  // namespace hwsec
