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
using tpm_manager::TpmManagerStatus;
namespace hwsec {

class BackendDerivingTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendDerivingTpm1Test, SecureDerive) {
  const brillo::SecureBlob kFakeBlob("blob");

  auto result = middleware_->CallSync<&Backend::Deriving::SecureDerive>(
      Key{.token = 0}, kFakeBlob);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeBlob);
}

TEST_F(BackendDerivingTpm1Test, Derive) {
  const brillo::Blob kFakeBlob = brillo::BlobFromString("blob");

  auto result = middleware_->CallSync<&Backend::Deriving::Derive>(
      Key{.token = 0}, kFakeBlob);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeBlob);
}

}  // namespace hwsec
