// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <cstdint>
#include <gtest/gtest.h>

#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

// Prevent the conflict definition from tss.h
#undef TPM_ALG_RSA

using hwsec_foundation::Sha256;
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

class BackendEncryptionTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendEncryptionTpm2Test, Encrypt) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeBlob = "fake_blob";
  const std::string kFakeOutput = "fake_output";
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent,
      .auth_policy = trunks::TPM2B_DIGEST{.size = 0},
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(key.ok());

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              AsymmetricEncrypt(kFakeKeyHandle, trunks::TPM_ALG_OAEP,
                                trunks::TPM_ALG_SHA256, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(kFakeOutput), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Encryption::Encrypt>(
      key->GetKey(), brillo::SecureBlob(kFakeBlob),
      Backend::Encryption::EncryptionOptions{});

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, brillo::BlobFromString(kFakeOutput));
}

TEST_F(BackendEncryptionTpm2Test, EncryptNullAlgo) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeBlob = "fake_blob";
  const std::string kFakeOutput = "fake_output";
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent,
      .auth_policy = trunks::TPM2B_DIGEST{.size = 0},
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(key.ok());

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              AsymmetricEncrypt(kFakeKeyHandle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(kFakeOutput), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Encryption::Encrypt>(
      key->GetKey(), brillo::SecureBlob(kFakeBlob),
      Backend::Encryption::EncryptionOptions{
          .schema = Backend::Encryption::EncryptionOptions::Schema::kNull,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, brillo::BlobFromString(kFakeOutput));
}

TEST_F(BackendEncryptionTpm2Test, Decrypt) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeBlob = "fake_blob";
  const std::string kFakeOutput = "fake_output";
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent,
      .auth_policy = trunks::TPM2B_DIGEST{.size = 0},
  };

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(key.ok());

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              AsymmetricDecrypt(kFakeKeyHandle, trunks::TPM_ALG_OAEP,
                                trunks::TPM_ALG_SHA256, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(kFakeOutput), Return(trunks::TPM_RC_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::Encryption::Decrypt>(
      key->GetKey(), brillo::BlobFromString(kFakeBlob),
      Backend::Encryption::EncryptionOptions{});

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, brillo::SecureBlob(kFakeOutput));
}

}  // namespace hwsec
