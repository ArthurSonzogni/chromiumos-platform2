// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

// Prevent the conflict definition from tss.h
#undef TPM_ALG_RSA

using brillo::BlobFromString;
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

namespace {

bool GenerateRsaKey(int key_size_bits,
                    crypto::ScopedEVP_PKEY* pkey,
                    brillo::Blob* key_spki_der) {
  crypto::ScopedEVP_PKEY_CTX pkey_context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  if (!pkey_context)
    return false;
  if (EVP_PKEY_keygen_init(pkey_context.get()) <= 0)
    return false;
  if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_context.get(), key_size_bits) <=
      0) {
    return false;
  }
  EVP_PKEY* pkey_raw = nullptr;
  if (EVP_PKEY_keygen(pkey_context.get(), &pkey_raw) <= 0)
    return false;
  pkey->reset(pkey_raw);
  // Obtain the DER-encoded Subject Public Key Info.
  const int key_spki_der_length = i2d_PUBKEY(pkey->get(), nullptr);
  if (key_spki_der_length < 0)
    return false;
  key_spki_der->resize(key_spki_der_length);
  unsigned char* key_spki_der_buffer = key_spki_der->data();
  return i2d_PUBKEY(pkey->get(), &key_spki_der_buffer) == key_spki_der->size();
}

}  // namespace

class BackendKeyManagementTpm2Test : public BackendTpm2TestBase {};

TEST_F(BackendKeyManagementTpm2Test, GetSupportedAlgo) {
  auto result =
      middleware_->CallSync<&Backend::KeyManagement::GetSupportedAlgo>();

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->count(KeyAlgoType::kRsa));
  EXPECT_TRUE(result->count(KeyAlgoType::kEcc));
}

TEST_F(BackendKeyManagementTpm2Test, CreateSoftwareRsaKey) {
  const OperationPolicySetting kFakePolicy{};
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kRsa;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              ImportRSAKey(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey,
                           _, _, _, "", _, _))
      .WillOnce(DoAll(SetArgPointee<6>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = false,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, CreateRsaKey) {
  const OperationPolicySetting kFakePolicy{};
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kRsa;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      CreateRSAKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       _, "", "", false, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<8>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = false,
          .allow_decrypt = true,
          .allow_sign = false,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, CreateEccKey) {
  const OperationPolicySetting kFakePolicy{};
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kEcc;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      CreateECCKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       "", "", false, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = false,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  auto result2 =
      middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
          result->key.GetKey());

  ASSERT_TRUE(result2.ok());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, LoadKey) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(result.ok());

  auto result2 =
      middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
          result->GetKey());

  ASSERT_TRUE(result2.ok());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, CreateAutoReloadKey) {
  const OperationPolicySetting kFakePolicy{};
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kEcc;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeKeyHandle2 = 0x7331;

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      CreateECCKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       "", "", false, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result =
      middleware_->CallSync<&Backend::KeyManagement::CreateAutoReloadKey>(
          kFakePolicy, kFakeAlgo,
          Backend::KeyManagement::CreateKeyOptions{
              .allow_software_gen = true,
              .allow_decrypt = true,
              .allow_sign = false,
          });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle2),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result2 =
      middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
          result->key.GetKey());

  ASSERT_TRUE(result2.ok());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle2, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, LoadAutoReloadKey) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeKeyHandle2 = 0x7331;

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result =
      middleware_->CallSync<&Backend::KeyManagement::LoadAutoReloadKey>(
          kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(result.ok());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle2),
                      Return(trunks::TPM_RC_SUCCESS)));

  auto result2 =
      middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
          result->GetKey());

  ASSERT_TRUE(result2.ok());

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle2, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, GetPersistentKey) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(trunks::kStorageRootKey, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .Times(0);

  {
    auto result =
        middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
            Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

    ASSERT_TRUE(result.ok());

    auto result2 =
        middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
            Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

    ASSERT_TRUE(result2.ok());
  }

  auto result3 =
      middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
          Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

  ASSERT_TRUE(result3.ok());
}

TEST_F(BackendKeyManagementTpm2Test, GetRsaPubkeyHash) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
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

  auto result = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(result.ok());

  auto result2 = middleware_->CallSync<&Backend::KeyManagement::GetPubkeyHash>(
      result->GetKey());

  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(*result2, Sha256(BlobFromString("9876543210")));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, GetEccPubkeyHash) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
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

  auto result = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob));

  ASSERT_TRUE(result.ok());

  auto result2 = middleware_->CallSync<&Backend::KeyManagement::GetPubkeyHash>(
      result->GetKey());

  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(*result2, Sha256(BlobFromString("0123456789")));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, SideLoadKey) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .Times(0);

  auto result = middleware_->CallSync<&Backend::KeyManagement::SideLoadKey>(
      kFakeKeyHandle);

  ASSERT_TRUE(result.ok());

  auto result2 = middleware_->CallSync<&Backend::KeyManagement::GetKeyHandle>(
      result->GetKey());

  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(*result2, kFakeKeyHandle);
}

TEST_F(BackendKeyManagementTpm2Test, PolicyRsaKey) {
  const std::string kFakeAuthValue = "fake_auth_value";
  const OperationPolicySetting kFakePolicy{
      .device_config_settings =
          DeviceConfigSettings{
              .boot_mode =
                  DeviceConfigSettings::BootModeSetting{
                      .mode = std::nullopt,
                  },
          },
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob(kFakeAuthValue),
          },
  };
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kRsa;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kFakePolicyDigest = "fake_policy_digest";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakePolicyDigest),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      CreateRSAKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       _, kFakeAuthValue, kFakePolicyDigest, true, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<8>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = false,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, PolicyEccKey) {
  const std::string kFakeAuthValue = "fake_auth_value";
  const OperationPolicySetting kFakePolicy{
      .device_config_settings =
          DeviceConfigSettings{
              .boot_mode =
                  DeviceConfigSettings::BootModeSetting{
                      .mode = std::nullopt,
                  },
          },
      .permission =
          Permission{
              .auth_value = brillo::SecureBlob(kFakeAuthValue),
          },
  };
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kEcc;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kFakePolicyDigest = "fake_policy_digest";
  const uint32_t kFakeKeyHandle = 0x1337;

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakePolicyDigest),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().tpm_utility,
      CreateECCKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       kFakeAuthValue, kFakePolicyDigest, true, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility, LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().tpm_utility,
              GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = false,
      });

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->key_blob, brillo::BlobFromString(kFakeKeyBlob));

  EXPECT_CALL(proxy_->GetMock().tpm, FlushContextSync(kFakeKeyHandle, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
}

TEST_F(BackendKeyManagementTpm2Test, LoadPublicKeyFromSpki) {
  crypto::ScopedEVP_PKEY pkey;
  brillo::Blob public_key_spki_der;
  EXPECT_TRUE(GenerateRsaKey(2048, &pkey, &public_key_spki_der));

  // A workaround the get the internal derived class.
  BackendTpm2::KeyManagementTpm2* key_management =
      dynamic_cast<BackendTpm2::KeyManagementTpm2*>(
          backend_->Get<Backend::KeyManagement>());
  ASSERT_NE(key_management, nullptr);

  auto result = key_management->LoadPublicKeyFromSpki(
      public_key_spki_der, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA384);

  ASSERT_TRUE(result.ok());
}

TEST_F(BackendKeyManagementTpm2Test, LoadPublicKeyFromSpkiFailed) {
  // Wrong format key.
  brillo::Blob public_key_spki_der(64, '?');

  // A workaround the get the internal derived class.
  BackendTpm2::KeyManagementTpm2* key_management =
      dynamic_cast<BackendTpm2::KeyManagementTpm2*>(
          backend_->Get<Backend::KeyManagement>());
  ASSERT_NE(key_management, nullptr);

  auto result = key_management->LoadPublicKeyFromSpki(
      public_key_spki_der, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA384);

  EXPECT_FALSE(result.ok());
}

}  // namespace hwsec
