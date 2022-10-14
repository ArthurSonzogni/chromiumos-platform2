// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
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

class BackendKeyManagementTpm1Test : public BackendTpm1TestBase {};

TEST_F(BackendKeyManagementTpm1Test, GetSupportedAlgo) {
  auto result =
      middleware_->CallSync<&Backend::KeyManagement::GetSupportedAlgo>();

  ASSERT_OK(result);
  EXPECT_TRUE(result->count(KeyAlgoType::kRsa));
  EXPECT_FALSE(result->count(KeyAlgoType::kEcc));
}

TEST_F(BackendKeyManagementTpm1Test, GetPersistentKey) {
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeSrkAuthUsage = 0x9876;
  const uint32_t kFakeSrkUsagePolicy = 0x1283;

  tpm_manager::GetTpmNonsensitiveStatusReply reply;
  reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  reply.set_is_owned(true);
  EXPECT_CALL(proxy_->GetMock().tpm_manager,
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(reply), Return(true)));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByUUID(kDefaultContext, TSS_PS_TYPE_SYSTEM, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, _))
      .WillOnce(
          DoAll(SetArgPointee<3>(kFakeSrkAuthUsage), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetPolicyObject(kFakeKeyHandle, TSS_POLICY_USAGE, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(kFakeSrkUsagePolicy), Return(TPM_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Policy_SetSecret(kFakeSrkUsagePolicy, TSS_SECRET_MODE_PLAIN, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  {
    auto result =
        middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
            Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

    EXPECT_THAT(result, IsOk());

    auto result2 =
        middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
            Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

    EXPECT_THAT(result2, IsOk());
  }

  auto result3 =
      middleware_->CallSync<&Backend::KeyManagement::GetPersistentKey>(
          Backend::KeyManagement::PersistentKeyType::kStorageRootKey);

  EXPECT_THAT(result3, IsOk());
}

TEST_F(BackendKeyManagementTpm1Test, CreateSoftwareGenRsaKey) {
  const OperationPolicySetting kFakePolicy{};
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kRsa;
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeKeyHandle2 = 0x1338;
  const uint32_t kFakePolicyHandle = 0x7331;

  SetupSrk();

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_RSAKEY, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_SIGSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_ENCSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_POLICY,
                                        TSS_POLICY_MIGRATION, _))
      .WillOnce(
          DoAll(SetArgPointee<3>(kFakePolicyHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Policy_SetSecret(kFakePolicyHandle, TSS_SECRET_MODE_PLAIN, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Policy_AssignToObject(kFakePolicyHandle, kFakeKeyHandle))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribData(kFakeKeyHandle, TSS_TSPATTRIB_RSAKEY_INFO,
                                 TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribData(kFakeKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_PRIVATE_KEY, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_WrapKey(kFakeKeyHandle, kDefaultSrkHandle, 0))
      .WillOnce(Return(TPM_SUCCESS));

  brillo::Blob key_blob = kFakeKeyBlob;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetAttribData(kFakeKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_BLOB, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(key_blob.size()),
                      SetArgPointee<4>(key_blob.data()), Return(TPM_SUCCESS)));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle2), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle2, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::KeyManagement::CreateKey>(
      kFakePolicy, kFakeAlgo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = true,
      });

  ASSERT_OK(result);
  EXPECT_EQ(result->key_blob, kFakeKeyBlob);
}

TEST_F(BackendKeyManagementTpm1Test, CreateRsaKey) {
  const OperationPolicySetting kFakePolicy{
      .device_config_settings =
          DeviceConfigSettings{
              .boot_mode =
                  DeviceConfigSettings::BootModeSetting{
                      .mode = std::nullopt,
                  },
          },
  };
  const KeyAlgoType kFakeAlgo = KeyAlgoType::kRsa;
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakePcrHandle = 0x7331;

  SetupSrk();

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_PCRS,
                                        TSS_PCRS_STRUCT_INFO, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakePcrHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_PcrComposite_SetPcrValue(kFakePcrHandle, 0, _, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_RSAKEY, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_SIGSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_SetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_ENCSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Key_CreateKey(kFakeKeyHandle, kDefaultSrkHandle, kFakePcrHandle))
      .WillOnce(Return(TPM_SUCCESS));

  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_LoadKey(kFakeKeyHandle, kDefaultSrkHandle))
      .WillOnce(Return(TPM_SUCCESS));

  brillo::Blob key_blob = kFakeKeyBlob;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_GetAttribData(kFakeKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_BLOB, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(key_blob.size()),
                      SetArgPointee<4>(key_blob.data()), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto result =
      middleware_->CallSync<&Backend::KeyManagement::CreateAutoReloadKey>(
          kFakePolicy, kFakeAlgo,
          Backend::KeyManagement::CreateKeyOptions{
              .allow_software_gen = true,
              .allow_decrypt = true,
              .allow_sign = true,
          });

  ASSERT_OK(result);
  EXPECT_EQ(result->key_blob, kFakeKeyBlob);
}

TEST_F(BackendKeyManagementTpm1Test, LoadKey) {
  const OperationPolicy kFakePolicy{};
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;

  SetupSrk();

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::KeyManagement::LoadKey>(
      kFakePolicy, kFakeKeyBlob);

  ASSERT_OK(result);

  EXPECT_THAT(middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
                  result->GetKey()),
              IsOk());

  EXPECT_THAT(middleware_->CallSync<&Backend::KeyManagement::GetKeyHandle>(
                  result->GetKey()),
              IsOkAndHolds(kFakeKeyHandle));
}

TEST_F(BackendKeyManagementTpm1Test, LoadAutoReloadKey) {
  const OperationPolicy kFakePolicy{};
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;
  const uint32_t kFakeKeyHandle2 = 0x7331;

  SetupSrk();

  EXPECT_CALL(
      proxy_->GetMock().overalls,
      Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle), Return(TPM_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<4>(kFakeKeyHandle2), Return(TPM_SUCCESS)));

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto result =
      middleware_->CallSync<&Backend::KeyManagement::LoadAutoReloadKey>(
          kFakePolicy, kFakeKeyBlob);

  ASSERT_OK(result);

  EXPECT_THAT(middleware_->CallSync<&Backend::KeyManagement::ReloadIfPossible>(
                  result->GetKey()),
              IsOk());

  EXPECT_THAT(middleware_->CallSync<&Backend::KeyManagement::GetKeyHandle>(
                  result->GetKey()),
              IsOkAndHolds(kFakeKeyHandle2));
}

TEST_F(BackendKeyManagementTpm1Test, SideLoadKey) {
  const OperationPolicy kFakePolicy{};
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const uint32_t kFakeKeyHandle = 0x1337;

  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(proxy_->GetMock().overalls,
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  auto result = middleware_->CallSync<&Backend::KeyManagement::SideLoadKey>(
      kFakeKeyHandle);

  ASSERT_OK(result);

  EXPECT_THAT(middleware_->CallSync<&Backend::KeyManagement::GetKeyHandle>(
                  result->GetKey()),
              IsOkAndHolds(kFakeKeyHandle));
}

TEST_F(BackendKeyManagementTpm1Test, LoadPublicKeyFromSpki) {
  crypto::ScopedEVP_PKEY pkey;
  brillo::Blob public_key_spki_der;
  EXPECT_TRUE(GenerateRsaKey(2048, &pkey, &public_key_spki_der));

  EXPECT_THAT(
      backend_->GetKeyManagementTpm1().LoadPublicKeyFromSpki(
          public_key_spki_der, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA384),
      IsOk());
}

TEST_F(BackendKeyManagementTpm1Test, LoadPublicKeyFromSpkiFailed) {
  // Wrong format key.
  brillo::Blob public_key_spki_der(64, '?');

  EXPECT_THAT(
      backend_->GetKeyManagementTpm1().LoadPublicKeyFromSpki(
          public_key_spki_der, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA384),
      NotOk());
}

}  // namespace hwsec
