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
  void SetupGetPublicKeyDer(const brillo::Blob& fake_pubkey) {
    static constexpr uint8_t kFakeModulus[] = {
        0x00, 0xb1, 0x51, 0x8b, 0x94, 0x6a, 0xa1, 0x66, 0x91, 0xc5, 0x5a, 0xe5,
        0x9a, 0x8e, 0x33, 0x61, 0x04, 0x72, 0xf4, 0x4c, 0x28, 0x01, 0x01, 0x68,
        0x49, 0x2b, 0xcb, 0xba, 0x91, 0x11, 0xb8, 0xb0, 0x3d, 0x13, 0xb9, 0xf2,
        0x48, 0x40, 0x03, 0xe5, 0x9e, 0x57, 0x6e, 0xc9, 0xa2, 0xee, 0x12, 0x02,
        0x81, 0xde, 0x47, 0xff, 0x2f, 0xfc, 0x18, 0x71, 0xcf, 0x1a, 0xf6, 0xa7,
        0x13, 0x7c, 0x7d, 0x30, 0x3f, 0x40, 0xa2, 0x05, 0xed, 0x7d, 0x3a, 0x2f,
        0xcc, 0xbd, 0xd3, 0xd9, 0x1a, 0x76, 0xd1, 0xec, 0xd5, 0x42, 0xdb, 0x1d,
        0x64, 0x5e, 0x66, 0x00, 0x04, 0x75, 0x49, 0xb7, 0x40, 0x4d, 0xae, 0x8f,
        0xbd, 0x8b, 0x81, 0x8a, 0x34, 0xd8, 0xb9, 0x4d, 0xd2, 0xfe, 0xc9, 0x08,
        0x16, 0x6c, 0x32, 0x77, 0x2b, 0xad, 0x21, 0xa5, 0xaa, 0x3f, 0x00, 0xcf,
        0x19, 0x0a, 0x4e, 0xc2, 0x9b, 0x01, 0xef, 0x60, 0x60, 0x88, 0x33, 0x1e,
        0x62, 0xd7, 0x22, 0x56, 0x7b, 0xb1, 0x26, 0xd1, 0xe4, 0x4f, 0x0c, 0xfc,
        0xfc, 0xe7, 0x1f, 0x56, 0xef, 0x6c, 0x6a, 0xa4, 0x2f, 0xa2, 0x62, 0x62,
        0x2a, 0x89, 0xd2, 0x5c, 0x3f, 0x96, 0xc9, 0x7c, 0x54, 0x5f, 0xd6, 0xe2,
        0xa1, 0xa0, 0x59, 0xef, 0x57, 0xc5, 0xb2, 0xa8, 0x80, 0x04, 0xde, 0x29,
        0x14, 0x19, 0x9a, 0x0d, 0x49, 0x09, 0xd7, 0xbb, 0x9c, 0xc9, 0x15, 0x7a,
        0x33, 0x8a, 0x35, 0x14, 0x01, 0x4a, 0x65, 0x39, 0x8c, 0x68, 0x73, 0x91,
        0x8c, 0x70, 0xa7, 0x10, 0x7a, 0x3e, 0xff, 0xd6, 0x1b, 0xa7, 0x29, 0xad,
        0x35, 0x12, 0xeb, 0x0c, 0x26, 0xd5, 0x36, 0xa5, 0xfb, 0xab, 0x42, 0x7b,
        0xeb, 0xc9, 0x45, 0x3c, 0x6d, 0x69, 0x32, 0x36, 0xd0, 0x43, 0xf3, 0xc3,
        0x2d, 0x0a, 0xcd, 0x31, 0xf0, 0xea, 0xf3, 0x44, 0xa2, 0x00, 0x83, 0xf5,
        0x93, 0x57, 0x49, 0xd8, 0xf5,
    };
    static constexpr uint8_t kFakeParms[] = {0xde, 0xad, 0xbe, 0xef, 0x12,
                                             0x34, 0x56, 0x78, 0x90};
    // brillo::Blob mutable_fake_pubkey;
    EXPECT_CALL(proxy_->GetMockOveralls(),
                Orspi_UnloadBlob_PUBKEY_s(_, _, _, _))
        .With(Args<1, 2>(ElementsAreArray(fake_pubkey)))
        .WillOnce([&](uint64_t* offset, auto&&, auto&&,
                      TPM_PUBKEY* tpm_pubkey) {
          *offset = fake_pubkey.size();
          uint8_t* parms_ptr =
              static_cast<uint8_t*>(malloc(sizeof(kFakeParms)));
          memcpy(parms_ptr, kFakeParms, sizeof(kFakeParms));
          uint8_t* key_ptr =
              static_cast<uint8_t*>(malloc(sizeof(kFakeModulus)));
          memcpy(key_ptr, kFakeModulus, sizeof(kFakeModulus));
          *tpm_pubkey = TPM_PUBKEY{
              .algorithmParms =
                  TPM_KEY_PARMS{
                      .algorithmID = TPM_ALG_RSA,
                      .encScheme = TPM_ES_NONE,
                      .sigScheme = TPM_SS_NONE,
                      .parmSize = sizeof(kFakeParms),
                      .parms = parms_ptr,
                  },
              .pubKey =
                  TPM_STORE_PUBKEY{
                      .keyLength = static_cast<uint32_t>(sizeof(kFakeModulus)),
                      .key = key_ptr,
                  },
          };
          return TPM_SUCCESS;
        });
    EXPECT_CALL(proxy_->GetMockOveralls(),
                Orspi_UnloadBlob_RSA_KEY_PARMS_s(_, _, _, _))
        .With(Args<1, 2>(ElementsAreArray(kFakeParms)))
        .WillOnce(DoAll(SetArgPointee<0>(sizeof(kFakeParms)),
                        SetArgPointee<3>(TPM_RSA_KEY_PARMS{
                            .keyLength = 0,
                            .numPrimes = 0,
                            .exponentSize = 0,
                            .exponent = nullptr,
                        }),
                        Return(TPM_SUCCESS)));
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
  const std::string kFakePcrValue = "fake_pcr_value";
  const uint8_t value_size = static_cast<unsigned char>(kFakePcrValue.size());
  const uint8_t kFakeCompositeHeaderBuffer[] = {
      0, 2,                 // select_size = 2 (big-endian)
      1, 0,                 // select_bitmap = bit 0 selected
      0, 0, 0, value_size,  // value_size = kFakePcrValue.size() (big-endian)
  };
  const std::string kFakeCompositeHeader(std::begin(kFakeCompositeHeaderBuffer),
                                         std::end(kFakeCompositeHeaderBuffer));
  const std::string kFakePcrComposite = kFakeCompositeHeader + kFakePcrValue;
  const std::string kFakePcrDigest = base::SHA1HashString(kFakePcrComposite);
  const std::string kQuotedData = std::string('x', 8) + kFakePcrDigest;

  attestation::Quote fake_quote;
  fake_quote.set_quoted_pcr_value(kFakePcrValue);
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
  SetupGetPublicKeyDer(fake_pubkey);

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
                  kDefaultOwnerTpm, kDefaultSrkHandle, kFakePcaKeyHandle,
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
  SetupGetPublicKeyDer(fake_identity_pubkey);
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
}

}  // namespace hwsec
