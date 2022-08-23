// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

// Prevent the conflict definition from tss.h
#undef TPM_ALG_RSA

using hwsec_foundation::error::testing::IsOk;
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

class BackendSigningTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendSigningTpm2Test, SignRSA) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent,
      .auth_policy = trunks::TPM2B_DIGEST{.size = 0},
      .parameters =
          trunks::TPMU_PUBLIC_PARMS{
              .rsa_detail =
                  trunks::TPMS_RSA_PARMS{
                      .symmetric =
                          trunks::TPMT_SYM_DEF_OBJECT{
                              .algorithm = trunks::TPM_ALG_NULL,
                          },
                      .scheme =
                          trunks::TPMT_RSA_SCHEME{
                              .scheme = trunks::TPM_ALG_NULL,
                          },
                      .key_bits = 2048,
                      .exponent = 0,
                  },
          },
      .unique =
          trunks::TPMU_PUBLIC_ID{
              .rsa =
                  trunks::TPM2B_PUBLIC_KEY_RSA{
                      .size = 10,
                      .buffer = "9876543210",
                  },
          },
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
              Sign(kFakeKeyHandle, trunks::TPM_ALG_RSASSA,
                   trunks::TPM_ALG_SHA256, kDataToSign, true, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  auto signature = middleware_->CallSync<&Backend::Signing::Sign>(
      kFakePolicy, key->GetKey(), brillo::BlobFromString(kDataToSign));

  ASSERT_TRUE(signature.ok());
  EXPECT_EQ(*signature, brillo::BlobFromString(kSignature));
}

TEST_F(BackendSigningTpm2Test, SignECC) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_ECC,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent,
      .auth_policy = trunks::TPM2B_DIGEST{.size = 0},
      .parameters =
          trunks::TPMU_PUBLIC_PARMS{
              .ecc_detail =
                  trunks::TPMS_ECC_PARMS{
                      .symmetric =
                          trunks::TPMT_SYM_DEF_OBJECT{
                              .algorithm = trunks::TPM_ALG_NULL,
                          },
                      .scheme =
                          trunks::TPMT_ECC_SCHEME{
                              .scheme = trunks::TPM_ALG_NULL,
                          },
                      .curve_id = trunks::TPM_ECC_NIST_P256,
                      .kdf =
                          trunks::TPMT_KDF_SCHEME{
                              .scheme = trunks::TPM_ALG_NULL,
                          },
                  },
          },
      .unique =
          trunks::TPMU_PUBLIC_ID{
              .ecc =
                  trunks::TPMS_ECC_POINT{
                      .x =
                          trunks::TPM2B_ECC_PARAMETER{
                              .size = 10,
                              .buffer = "0123456789",
                          },
                      .y = trunks::TPM2B_ECC_PARAMETER{.size = 0},
                  },
          },
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

  ASSERT_THAT(key, IsOk());

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              Sign(kFakeKeyHandle, trunks::TPM_ALG_ECDSA,
                   trunks::TPM_ALG_SHA256, kDataToSign, true, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  auto signature = middleware_->CallSync<&Backend::Signing::Sign>(
      kFakePolicy, key->GetKey(), brillo::BlobFromString(kDataToSign));

  ASSERT_THAT(signature, IsOk());
  EXPECT_EQ(*signature, brillo::BlobFromString(kSignature));
}

TEST_F(BackendSigningTpm2Test, SignUnknown) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_KEYEDHASH,
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

  ASSERT_THAT(key, IsOk());

  auto signature = middleware_->CallSync<&Backend::Signing::Sign>(
      kFakePolicy, key->GetKey(), brillo::BlobFromString(kDataToSign));

  EXPECT_FALSE(signature.ok());
}

}  // namespace hwsec
