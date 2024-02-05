// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <base/hash/sha1.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/sha.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"
#include "libhwsec/backend/tpm1/static_utils.h"
#include "libhwsec/overalls/mock_overalls.h"
#include "libhwsec/structures/key.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using testing::_;
using testing::Args;
using testing::DoAll;
using testing::ElementsAreArray;
using testing::Not;
using testing::Return;
using testing::SetArgPointee;
using testing::SetArrayArgument;

namespace hwsec {

namespace {

constexpr unsigned int kDefaultTpmRsaKeyBits = 2048;
constexpr int kKeySizeBytes = kDefaultTpmRsaKeyBits / 8;

MATCHER_P(HasBits, bits, "") {
  return bits == (bits & arg);
}

}  // namespace

class BackendAttestationTpm1Test : public BackendTpm1TestBase {
 protected:
  StatusOr<ScopedKey> LoadFakeKey(const uint32_t fake_key_handle) {
    const OperationPolicy kFakePolicy{};
    const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
    const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");

    SetupSrk();

    EXPECT_CALL(
        proxy_->GetMockOveralls(),
        Ospi_Context_LoadKeyByBlob(kDefaultContext, kDefaultSrkHandle, _, _, _))
        .WillOnce(
            DoAll(SetArgPointee<4>(fake_key_handle), Return(TPM_SUCCESS)));

    brillo::Blob fake_pubkey = kFakePubkey;
    EXPECT_CALL(proxy_->GetMockOveralls(),
                Ospi_Key_GetPubKey(fake_key_handle, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_pubkey.size()),
                        SetArgPointee<2>(fake_pubkey.data()),
                        Return(TPM_SUCCESS)));

    return backend_->GetKeyManagementTpm1().LoadKey(
        kFakePolicy, kFakeKeyBlob, Backend::KeyManagement::LoadKeyOptions{});
  }
};

TEST_F(BackendAttestationTpm1Test, Quote) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const brillo::Blob kNonZeroPcr(SHA_DIGEST_LENGTH, 'X');
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeQuotedData = "fake_quoted_data";
  const std::string kFakeQuote = "fake_quote";
  std::string fake_quoted_data = kFakeQuotedData;
  std::string fake_quote = kFakeQuote;
  TSS_VALIDATION fake_validation = {
      .ulDataLength = static_cast<UINT32>(fake_quoted_data.size()),
      .rgbData = reinterpret_cast<unsigned char*>(fake_quoted_data.data()),
      .ulValidationDataLength = static_cast<UINT32>(fake_quote.size()),
      .rgbValidationData = reinterpret_cast<unsigned char*>(fake_quote.data()),
  };

  auto load_key_result = LoadFakeKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  brillo::Blob non_zero_pcr = kNonZeroPcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(non_zero_pcr.size()),
                      SetArgPointee<3>(non_zero_pcr.data()),
                      Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_Quote(_, kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_validation), Return(TPM_SUCCESS)));

  auto result = backend_->GetAttestationTpm1().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_pcr_value());
  EXPECT_EQ(result->quoted_pcr_value(), BlobToString(kNonZeroPcr));
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_EQ(result->quote(), "fake_quote");
  EXPECT_FALSE(result->has_pcr_source_hint());
}

TEST_F(BackendAttestationTpm1Test, QuoteDeviceModel) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kDeviceModel};
  const brillo::Blob kNonZeroPcr(SHA_DIGEST_LENGTH, 'X');
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeQuotedData = "fake_quoted_data";
  const std::string kFakeQuote = "fake_quote";
  std::string fake_quoted_data = kFakeQuotedData;
  std::string fake_quote = kFakeQuote;
  TSS_VALIDATION fake_validation = {
      .ulDataLength = static_cast<UINT32>(fake_quoted_data.size()),
      .rgbData = reinterpret_cast<unsigned char*>(fake_quoted_data.data()),
      .ulValidationDataLength = static_cast<UINT32>(fake_quote.size()),
      .rgbValidationData = reinterpret_cast<unsigned char*>(fake_quote.data()),
  };
  proxy_->GetFakeCrossystem().VbSetSystemPropertyString("hwid",
                                                        "fake_pcr_source_hint");

  auto load_key_result = LoadFakeKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  brillo::Blob non_zero_pcr = kNonZeroPcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(non_zero_pcr.size()),
                      SetArgPointee<3>(non_zero_pcr.data()),
                      Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_Quote(_, kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_validation), Return(TPM_SUCCESS)));

  auto result = backend_->GetAttestationTpm1().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_pcr_value());
  EXPECT_EQ(result->quoted_pcr_value(), BlobToString(kNonZeroPcr));
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_EQ(result->quote(), "fake_quote");
  ASSERT_TRUE(result->has_pcr_source_hint());
  EXPECT_EQ(result->pcr_source_hint(), "fake_pcr_source_hint");
}

TEST_F(BackendAttestationTpm1Test, QuoteMultipleDeviceConfigs) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode, DeviceConfig::kCurrentUser};
  const uint32_t kFakeKeyHandle = 0x1337;
  const std::string kFakeQuotedData = "fake_quoted_data";
  const std::string kFakeQuote = "fake_quote";
  std::string fake_quoted_data = kFakeQuotedData;
  std::string fake_quote = kFakeQuote;
  TSS_VALIDATION fake_validation = {
      .ulDataLength = static_cast<UINT32>(fake_quoted_data.size()),
      .rgbData = reinterpret_cast<unsigned char*>(fake_quoted_data.data()),
      .ulValidationDataLength = static_cast<UINT32>(fake_quote.size()),
      .rgbValidationData = reinterpret_cast<unsigned char*>(fake_quote.data()),
  };

  auto load_key_result = LoadFakeKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_Quote(_, kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_validation), Return(TPM_SUCCESS)));

  auto result = backend_->GetAttestationTpm1().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  EXPECT_FALSE(result->has_quoted_pcr_value());
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_EQ(result->quote(), "fake_quote");
  EXPECT_FALSE(result->has_pcr_source_hint());
}

TEST_F(BackendAttestationTpm1Test, QuoteFailure) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const brillo::Blob kNonZeroPcr(SHA_DIGEST_LENGTH, 'X');
  const uint32_t kFakeKeyHandle = 0x1337;

  auto load_key_result = LoadFakeKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  brillo::Blob non_zero_pcr = kNonZeroPcr;
  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(non_zero_pcr.size()),
                      SetArgPointee<3>(non_zero_pcr.data()),
                      Return(TPM_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_Quote(_, kFakeKeyHandle, _, _))
      .WillOnce(Return(TSS_E_FAIL));

  auto result = backend_->GetAttestationTpm1().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_NOT_OK(result);
}

TEST_F(BackendAttestationTpm1Test, IsQuoted) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const DeviceConfigSettings::BootModeSetting::Mode fake_mode = {
      .developer_mode = false,
      .recovery_mode = true,
      .verified_firmware = false,
  };
  brillo::Blob valid_pcr_blob = GetTpm1PCRValueForMode(fake_mode);

  EXPECT_CALL(proxy_->GetMockOveralls(), Ospi_TPM_PcrRead(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(valid_pcr_blob.size()),
                      SetArgPointee<3>(valid_pcr_blob.data()),
                      Return(TPM_SUCCESS)));

  const std::string valid_pcr = BlobToString(valid_pcr_blob);
  const uint8_t value_size = static_cast<unsigned char>(valid_pcr.size());
  const uint8_t kFakeCompositeHeaderBuffer[] = {
      0, 2,                 // select_size = 2 (big-endian)
      1, 0,                 // select_bitmap = bit 0 selected
      0, 0, 0, value_size,  // value_size = valid_pcr.size() (big-endian)
  };
  const std::string kFakeCompositeHeader(std::begin(kFakeCompositeHeaderBuffer),
                                         std::end(kFakeCompositeHeaderBuffer));
  const std::string kFakePcrComposite = kFakeCompositeHeader + valid_pcr;
  const std::string kFakePcrDigest = base::SHA1HashString(kFakePcrComposite);
  const std::string kQuotedData = std::string('x', 8) + kFakePcrDigest;

  attestation::Quote fake_quote;
  fake_quote.set_quoted_pcr_value(valid_pcr);
  fake_quote.set_quoted_data(kQuotedData);
  auto result =
      backend_->GetAttestationTpm1().IsQuoted(kFakeDeviceConfigs, fake_quote);
  ASSERT_OK(result);
}

TEST_F(BackendAttestationTpm1Test, CreateCertifiedKey) {
  const uint32_t kFakeIdentityHandle = 0x1773;
  const uint32_t kFakePcrHandle = 0;
  const uint32_t kFakeKeyHandle = 0x1337;
  const brillo::Blob kFakeKeyBlob = brillo::BlobFromString("fake_key_blob");
  const brillo::Blob kFakePubkey = brillo::BlobFromString("fake_pubkey");
  const std::string kFakeCertifyInfo = "fake_certify_info";
  const std::string kFakeSignature = "fake_signature";
  const attestation::KeyType kFakeKeyType = attestation::KeyType::KEY_TYPE_RSA;
  const attestation::KeyUsage kFakeKeyUsage =
      attestation::KeyUsage::KEY_USAGE_DECRYPT;

  // Load the identity key.
  auto load_key_result = LoadFakeKey(kFakeIdentityHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_identity_key = load_key_result.value();

  // Setup CreateKey
  brillo::Blob fake_key_blob = kFakeKeyBlob;
  brillo::Blob fake_pubkey = kFakePubkey;
  EXPECT_CALL(
      proxy_->GetMockOveralls(),
      Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_RSAKEY, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakeKeyHandle), Return(TPM_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_SetAttribUint32(kFakeKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_ENCSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(
      proxy_->GetMockOveralls(),
      Ospi_Key_CreateKey(kFakeKeyHandle, kDefaultSrkHandle, kFakePcrHandle))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Key_LoadKey(kFakeKeyHandle, kDefaultSrkHandle))
      .WillOnce(Return(TPM_SUCCESS));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetAttribData(kFakeKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_BLOB, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_key_blob.size()),
                      SetArgPointee<4>(fake_key_blob.data()),
                      Return(TPM_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Key_GetPubKey(kFakeKeyHandle, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_pubkey.size()),
                      SetArgPointee<2>(fake_pubkey.data()),
                      Return(TPM_SUCCESS)));

  // Setup CertifyKey
  std::string fake_certify_info = kFakeCertifyInfo;
  std::string fake_signature = kFakeSignature;
  TSS_VALIDATION fake_validation = {
      .ulDataLength = static_cast<UINT32>(fake_certify_info.size()),
      .rgbData = reinterpret_cast<unsigned char*>(fake_certify_info.data()),
      .ulValidationDataLength = static_cast<UINT32>(fake_signature.size()),
      .rgbValidationData =
          reinterpret_cast<unsigned char*>(fake_signature.data()),
  };
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Key_CertifyKey(kFakeKeyHandle, kFakeIdentityHandle, _))
      .WillOnce(DoAll(SetArgPointee<2>(fake_validation), Return(TPM_SUCCESS)));

  // Setup GetPublicKeyDer
  brillo::Blob fake_public_key_der = SetupGetPublicKeyDer(fake_pubkey);

  std::string external_data = "external_data";
  auto result = backend_->GetAttestationTpm1().CreateCertifiedKey(
      fake_identity_key.GetKey(), kFakeKeyType, kFakeKeyUsage,
      KeyRestriction::kUnrestricted, EndorsementAuth::kNoEndorsement,
      external_data);
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_key_blob());
  ASSERT_TRUE(result->has_public_key());
  ASSERT_TRUE(result->has_public_key_tpm_format());
  ASSERT_TRUE(result->has_certified_key_info());
  ASSERT_TRUE(result->has_certified_key_proof());
  ASSERT_TRUE(result->has_key_type());
  ASSERT_TRUE(result->has_key_usage());
  EXPECT_EQ(result->key_blob(), BlobToString(kFakeKeyBlob));
  EXPECT_EQ(result->public_key(), BlobToString(fake_public_key_der));
  EXPECT_EQ(result->public_key_tpm_format(), BlobToString(kFakePubkey));
  EXPECT_EQ(result->certified_key_info(), kFakeCertifyInfo);
  EXPECT_EQ(result->certified_key_proof(), kFakeSignature);
  EXPECT_EQ(result->key_type(), kFakeKeyType);
  EXPECT_EQ(result->key_usage(), kFakeKeyUsage);
}

TEST_F(BackendAttestationTpm1Test, CreateIdentity) {
  attestation::KeyType kFakeKeyType = attestation::KEY_TYPE_RSA;
  const uint32_t kFakePcaKeyHandle = 0x1337;
  const uint32_t kFakeIdentityKeyHandle = 0x7331;
  const brillo::Blob kFakePcaPubkey = BlobFromString("fake_pca_pubkey");
  const brillo::Blob kFakeIdentityLabel = BlobFromString("fake_identity_label");
  const brillo::Blob kFakeRequest = BlobFromString("fake_request");
  const brillo::Blob kFakeIdentityPubkey =
      BlobFromString("fake_identity_pubkey");
  const brillo::Blob kFakeIdentityKeyBlob =
      BlobFromString("fake_identity_key_blob");

  const brillo::Blob kFakeSymBlob = BlobFromString("fake_sym_blob");
  uint8_t* fake_asym_blob_ptr = static_cast<uint8_t*>(malloc(kKeySizeBytes));
  uint8_t* fake_sym_blob_ptr =
      static_cast<uint8_t*>(malloc(kFakeSymBlob.size()));
  memcpy(fake_sym_blob_ptr, kFakeSymBlob.data(), kFakeSymBlob.size());
  TPM_IDENTITY_REQ fake_identity_req{
      .asymSize = static_cast<UINT32>(kKeySizeBytes),
      .symSize = static_cast<UINT32>(kFakeSymBlob.size()),
      .asymBlob = fake_asym_blob_ptr,
      .symBlob = fake_sym_blob_ptr,
  };

  const TPM_ALGORITHM_ID kFakeSymetricKeyAlg = TPM_ALG_RSA;
  const brillo::Blob kFakeSymmetricKeyData =
      BlobFromString("fake_symmetric_key_data");
  uint8_t* fake_symmetric_key_data_ptr =
      static_cast<uint8_t*>(malloc(kFakeSymmetricKeyData.size()));
  memcpy(fake_symmetric_key_data_ptr, kFakeSymmetricKeyData.data(),
         kFakeSymmetricKeyData.size());
  TPM_SYMMETRIC_KEY fake_symmetric_key{
      .algId = kFakeSymetricKeyAlg,
      .size = static_cast<UINT16>(kFakeSymmetricKeyData.size()),
      .data = fake_symmetric_key_data_ptr,
  };

  const brillo::Blob kFakeProofSerial(kFakeSymBlob.size(), 'x');
  const brillo::Blob kFakeIdentityBinding =
      BlobFromString("fake_identity_binding");

  uint8_t* fake_identity_binding_ptr =
      static_cast<uint8_t*>(malloc(kFakeIdentityBinding.size()));
  memcpy(fake_symmetric_key_data_ptr, kFakeIdentityBinding.data(),
         kFakeIdentityBinding.size());
  TPM_IDENTITY_PROOF fake_identity_proof{
      .identityBindingSize = static_cast<UINT32>(kFakeIdentityBinding.size()),
      .identityBinding = fake_identity_binding_ptr,
  };

  SetupSrk();
  SetupOwner();

  // Setup CreateRsaPublicKeyObject for fake PCA key.
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_RSAKEY,
                                        Not(HasBits(TSS_KEY_TYPE_IDENTITY)), _))
      .WillOnce(
          DoAll(SetArgPointee<3>(kFakePcaKeyHandle), Return(TPM_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_SetAttribData(kFakePcaKeyHandle, TSS_TSPATTRIB_RSAKEY_INFO,
                                 TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, unsigned int size, BYTE* data) {
        // Create the encrypted asym_blob for the future decryption.
        brillo::Blob modulus(data, data + size);
        crypto::ScopedRSA rsa = hwsec_foundation::CreateRSAFromNumber(
            modulus, hwsec_foundation::kWellKnownExponent);
        const brillo::SecureBlob random_text =
            hwsec_foundation::CreateSecureRandomBlob(32);
        int ret = RSA_public_encrypt(random_text.size(), random_text.data(),
                                     fake_asym_blob_ptr, rsa.get(),
                                     RSA_PKCS1_PADDING);
        EXPECT_NE(ret, -1);
        return TPM_SUCCESS;
      });
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_SetAttribUint32(kFakePcaKeyHandle, TSS_TSPATTRIB_KEY_INFO,
                                   TSS_TSPATTRIB_KEYINFO_ENCSCHEME, _))
      .WillOnce(Return(TPM_SUCCESS));

  brillo::Blob fake_pca_pubkey = kFakePcaPubkey;
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetAttribData(kFakePcaKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_pca_pubkey.size()),
                      SetArgPointee<4>(fake_pca_pubkey.data()),
                      Return(TPM_SUCCESS)));

  // Setup for creating identity key
  unsigned char* fake_identity_label_ptr =
      static_cast<uint8_t*>(malloc(kFakeIdentityLabel.size()));
  memcpy(fake_identity_label_ptr, kFakeIdentityLabel.data(),
         kFakeIdentityLabel.size());
  EXPECT_CALL(proxy_->GetMockOveralls(), Orspi_Native_To_UNICODE(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeIdentityLabel.size()),
                      Return(fake_identity_label_ptr)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_Context_CreateObject(kDefaultContext, TSS_OBJECT_TYPE_RSAKEY,
                                        HasBits(TSS_KEY_TYPE_IDENTITY), _))
      .WillOnce(
          DoAll(SetArgPointee<3>(kFakeIdentityKeyHandle), Return(TPM_SUCCESS)));

  brillo::Blob fake_request = kFakeRequest;
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_TPM_CollateIdentityRequest(
                  kDefaultTpm, kDefaultSrkHandle, kFakePcaKeyHandle,
                  kFakeIdentityLabel.size(), fake_identity_label_ptr,
                  kFakeIdentityKeyHandle, TSS_ALG_3DES, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(fake_request.size()),
                      SetArgPointee<8>(fake_request.data()),
                      Return(TPM_SUCCESS)));

  // Setup DecryptIdentityRequest
  EXPECT_CALL(proxy_->GetMockOveralls(), Orspi_UnloadBlob_IDENTITY_REQ(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(fake_identity_req), Return(TPM_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Orspi_UnloadBlob_SYMMETRIC_KEY(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(fake_symmetric_key), Return(TPM_SUCCESS)));
  brillo::Blob fake_proof_serial = kFakeProofSerial;
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Orspi_SymDecrypt(kFakeSymetricKeyAlg, _, _, _, fake_sym_blob_ptr,
                               kFakeSymBlob.size(), _, _))
      .WillOnce(DoAll(SetArrayArgument<6>(fake_proof_serial.begin(),
                                          fake_proof_serial.end()),
                      SetArgPointee<7>(fake_proof_serial.size()),
                      Return(TPM_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Orspi_UnloadBlob_IDENTITY_PROOF(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(fake_identity_proof), Return(TPM_SUCCESS)));

  // Get the AIK public key.
  brillo::Blob fake_identity_pubkey = kFakeIdentityPubkey;
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetAttribData(kFakeIdentityKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_identity_pubkey.size()),
                      SetArgPointee<4>(fake_identity_pubkey.data()),
                      Return(TPM_SUCCESS)));
  // Setup GetPublicKeyDer
  brillo::Blob fake_public_key_der = SetupGetPublicKeyDer(fake_identity_pubkey);
  // Get the AIK blob so we can load it later.
  brillo::Blob fake_identity_key_blob = kFakeIdentityKeyBlob;
  EXPECT_CALL(proxy_->GetMockOveralls(),
              Ospi_GetAttribData(kFakeIdentityKeyHandle, TSS_TSPATTRIB_KEY_BLOB,
                                 TSS_TSPATTRIB_KEYBLOB_BLOB, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(fake_identity_key_blob.size()),
                      SetArgPointee<4>(fake_identity_key_blob.data()),
                      Return(TPM_SUCCESS)));

  auto result = backend_->GetAttestationTpm1().CreateIdentity(kFakeKeyType);
  ASSERT_OK(result);
  ASSERT_TRUE(result->identity_key.has_identity_public_key_der());
  EXPECT_EQ(result->identity_key.identity_public_key_der(),
            BlobToString(fake_public_key_der));
}

}  // namespace hwsec
