// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm2_error.h"
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

class TestingTPM2ErrorTest : public ::testing::Test {
 public:
  TestingTPM2ErrorTest() {}
  ~TestingTPM2ErrorTest() override = default;
};

TEST_F(TestingTPM2ErrorTest, CreateTPMErrorTest) {
  EXPECT_FALSE((TestForCreateError<TPM2Error>::Check::value));
  EXPECT_TRUE((TestForCreateError<TPM2Error, trunks::TPM_RC>::Check::value));
  EXPECT_FALSE((TestForCreateError<TPM2Error, std::string>::Check::value));
  auto err = CreateError<TPM2Error>(trunks::TPM_RC_SUCCESS);
  EXPECT_EQ(nullptr, err);
  err = CreateError<TPM2Error>(trunks::TPM_RC_HANDLE | trunks::TPM_RC_1);
  EXPECT_NE(nullptr, err);
}

TEST_F(TestingTPM2ErrorTest, TestForCreateErrorWrap) {
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, ErrorBase>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, ErrorBase,
                                       trunks::TPM_RC>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<ErrorBase, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPM2Error>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<ErrorBase, TPM2Error,
                                       trunks::TPM_RC>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<ErrorBase, TPM2Error, std::string>::Check::value));

  EXPECT_FALSE((TestForCreateErrorWrap<TPM2Error, ErrorBase>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPM2Error, ErrorBase,
                                      trunks::TPM_RC>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<TPM2Error, ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPM2Error, TPM2Error>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPM2Error, TPM2Error,
                                      trunks::TPM_RC>::Check::value));
  EXPECT_FALSE((
      TestForCreateErrorWrap<TPM2Error, TPM2Error, std::string>::Check::value));

  EXPECT_FALSE((TestForCreateErrorWrap<TPMError, TPM2Error>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TPMError, TPM2Error,
                                       trunks::TPM_RC>::Check::value));
  EXPECT_TRUE((
      TestForCreateErrorWrap<TPMError, TPM2Error, const char[]>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TPMError, TPM2Error, const char[],
                                      TPMRetryAction>::Check::value));
}

TEST_F(TestingTPM2ErrorTest, TPMRetryAction) {
  auto err = CreateError<TPM2Error>(trunks::TPM_RC_HANDLE | trunks::TPM_RC_1);
  EXPECT_EQ(err->ToTPMRetryAction(), TPMRetryAction::kLater);
  auto err2 = CreateErrorWrap<TPMError>(std::move(err), "OuO|||");
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("OuO|||: TPM2 error 0x18b (Handle 1: TPM_RC_HANDLE)", ss.str());
  EXPECT_EQ(err2->ToTPMRetryAction(), TPMRetryAction::kLater);
}

}  // namespace error
}  // namespace hwsec
