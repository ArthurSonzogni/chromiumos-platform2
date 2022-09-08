// Copyright 2022 The ChromiumOS Authors
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

class BackendRandomTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendRandomTpm1Test, RandomBlob) {
  const size_t kFakeSize = 42;
  const brillo::Blob kFakeData(kFakeSize, 'X');

  brillo::Blob fake_data = kFakeData;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_TPM_GetRandom(kDefaultTpm, kFakeSize, _))
      .WillOnce(DoAll(SetArgPointee<2>(fake_data.data()), Return(TPM_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Random::RandomBlob>(kFakeSize);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeData);
}

TEST_F(BackendRandomTpm1Test, RandomSecureBlob) {
  const size_t kFakeSize = 42;
  const brillo::SecureBlob kFakeData(kFakeSize, 'Y');

  brillo::SecureBlob fake_data = kFakeData;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_TPM_GetRandom(kDefaultTpm, kFakeSize, _))
      .WillOnce(DoAll(SetArgPointee<2>(fake_data.data()), Return(TPM_SUCCESS)));

  auto result =
      middleware_->CallSync<&Backend::Random::RandomSecureBlob>(kFakeSize);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, kFakeData);
}

}  // namespace hwsec
