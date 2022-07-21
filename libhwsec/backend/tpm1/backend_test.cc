// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace hwsec {

class BackendTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendTpm1Test, GetScopedTssContext) {
  TSS_HCONTEXT kFakeContext = 0x5566;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetScopedTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->value(), kFakeContext);
}

TEST_F(BackendTpm1Test, GetTssContext) {
  TSS_HCONTEXT kFakeContext = 0x1234;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeContext);

  // Run again to check the cache works correctly.
  result = backend_->GetTssContext();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeContext);
}

TEST_F(BackendTpm1Test, GetUserTpmHandle) {
  TSS_HCONTEXT kFakeContext = 0x1234;
  TSS_HTPM kFakeTpm = 0x5678;

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Create(_))
      .WillOnce(DoAll(SetArgPointee<0>(kFakeContext), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_Connect(kFakeContext, nullptr))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_GetTpmObject(kFakeContext, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeTpm), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls, Ospi_Context_Close(kFakeContext))
      .WillOnce(Return(TPM_SUCCESS));

  auto result = backend_->GetUserTpmHandle();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeTpm);

  // Run again to check the cache works correctly.
  result = backend_->GetUserTpmHandle();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeTpm);
}

}  // namespace hwsec
