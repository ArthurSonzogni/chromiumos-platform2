// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "libhwsec/error/tpm1_error.h"
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
class TestingTPM1ErrorTest : public ::testing::Test {
 public:
  TestingTPM1ErrorTest() {}
  ~TestingTPM1ErrorTest() override = default;
};
TEST_F(TestingTPM1ErrorTest, CreateTPMErrorTest) {
  EXPECT_FALSE((TestForCreateError<TPM1Error>::Check::value));
  EXPECT_TRUE((TestForCreateError<TPM1Error, TSS_RESULT>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPM1Error, std::string>::Check::value));
  auto err = CreateError<TPM1Error>(TSS_SUCCESS);
  EXPECT_EQ(nullptr, err);
  err = CreateError<TPM1Error>(TSS_LAYER_TCS | TSS_E_COMM_FAILURE);
  EXPECT_NE(nullptr, err);
}
TEST_F(TestingTPM1ErrorTest, TestForCreateErrorWrap) {
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<ErrorBase, ErrorBase, TSS_RESULT>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<ErrorBase, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPM1Error>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<ErrorBase, TPM1Error, TSS_RESULT>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<ErrorBase, TPM1Error, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPM1Error, ErrorBase>::Check::value));
  EXPECT_TRUE(
      (TestForCreateErrorWrap<TPM1Error, ErrorBase, TSS_RESULT>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<TPM1Error, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPM1Error, TPM1Error>::Check::value));
  EXPECT_TRUE(
      (TestForCreateErrorWrap<TPM1Error, TPM1Error, TSS_RESULT>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<TPM1Error, TPM1Error, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMError, TPM1Error>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TPMError, TPM1Error, TSS_RESULT>::Check::value));
  EXPECT_TRUE((
      TestForCreateErrorWrap<TPMError, TPM1Error, const char[]>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPM1Error, const char[],
                                      TPMRetryAction>::Check::value));
}
TEST_F(TestingTPM1ErrorTest, TPMRetryAction) {
  auto err = CreateError<TPM1Error>(TSS_LAYER_TCS | TSS_E_COMM_FAILURE);
  EXPECT_EQ(err->ToTPMRetryAction(), TPMRetryAction::kCommunication);
  auto err2 = CreateErrorWrap<TPMError>(std::move(err), "OuO");
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("OuO: TPM error 0x2011 (Communication failure)", ss.str());
  EXPECT_EQ(err2->ToTPMRetryAction(), TPMRetryAction::kCommunication);
}
}  // namespace error
}  // namespace hwsec
