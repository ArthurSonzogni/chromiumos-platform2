// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/sha.h>
#include <trunks/mock_blob_parser.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "libhwsec/structures/key.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace hwsec {

namespace {

constexpr trunks::TPMT_PUBLIC kDefaultRsaPublic = {
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

constexpr trunks::TPMT_PUBLIC kDefaultEccPublic = {
    .type = trunks::TPM_ALG_ECC,
};

}  // namespace

class BackendAttestationTpm2Test : public BackendTpm2TestBase {
 protected:
  StatusOr<ScopedKey> LoadFakeRSAKey(const uint32_t fake_key_handle) {
    const OperationPolicy kFakePolicy{};
    const std::string kFakeKeyBlob = "fake_key_blob";
    const trunks::TPMT_PUBLIC kFakePublic = kDefaultRsaPublic;

    EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
        .WillOnce(
            DoAll(SetArgPointee<2>(fake_key_handle), Return(TPM_RC_SUCCESS)));

    EXPECT_CALL(proxy_->GetMockTpmUtility(),
                GetKeyPublicArea(fake_key_handle, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFakePublic), Return(TPM_RC_SUCCESS)));

    return backend_->GetKeyManagementTpm2().LoadKey(
        kFakePolicy, BlobFromString(kFakeKeyBlob),
        Backend::KeyManagement::LoadKeyOptions{});
  }
  StatusOr<ScopedKey> LoadFakeECCKey(const uint32_t fake_key_handle) {
    const OperationPolicy kFakePolicy{};
    const std::string kFakeKeyBlob = "fake_key_blob";
    const trunks::TPMT_PUBLIC kFakePublic = kDefaultEccPublic;

    EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
        .WillOnce(
            DoAll(SetArgPointee<2>(fake_key_handle), Return(TPM_RC_SUCCESS)));

    EXPECT_CALL(proxy_->GetMockTpmUtility(),
                GetKeyPublicArea(fake_key_handle, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFakePublic), Return(TPM_RC_SUCCESS)));

    return backend_->GetKeyManagementTpm2().LoadKey(
        kFakePolicy, BlobFromString(kFakeKeyBlob),
        Backend::KeyManagement::LoadKeyOptions{});
  }
};

TEST_F(BackendAttestationTpm2Test, QuoteRsa) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const std::string kNonZeroPcr(SHA256_DIGEST_LENGTH, 'X');
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST("fake_quoted_data");
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_RSASSA,
      .signature.rsassa.sig = trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote"),
  };

  auto load_key_result = LoadFakeRSAKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), ReadPCR(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kNonZeroPcr), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeQuotedStruct),
                      SetArgPointee<6>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  auto result = backend_->GetAttestationTpm2().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_pcr_value());
  EXPECT_EQ(result->quoted_pcr_value(), kNonZeroPcr);
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_NE(result->quote().find("fake_quote"), std::string::npos);
  EXPECT_FALSE(result->has_pcr_source_hint());
}

TEST_F(BackendAttestationTpm2Test, QuoteEcc) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const std::string kNonZeroPcr(SHA256_DIGEST_LENGTH, 'X');
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST("fake_quoted_data");
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_ECDSA,
      .signature.ecdsa.signature_r =
          trunks::Make_TPM2B_ECC_PARAMETER("fake_quote_r"),
      .signature.ecdsa.signature_s =
          trunks::Make_TPM2B_ECC_PARAMETER("fake_quote_s"),
  };

  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), ReadPCR(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kNonZeroPcr), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeQuotedStruct),
                      SetArgPointee<6>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  auto result = backend_->GetAttestationTpm2().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_pcr_value());
  EXPECT_EQ(result->quoted_pcr_value(), kNonZeroPcr);
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_NE(result->quote().find("fake_quote_r"), std::string::npos);
  EXPECT_NE(result->quote().find("fake_quote_s"), std::string::npos);
  EXPECT_FALSE(result->has_pcr_source_hint());
}

TEST_F(BackendAttestationTpm2Test, QuoteDeviceModel) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kDeviceModel};
  const std::string kNonZeroPcr(SHA256_DIGEST_LENGTH, 'X');
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST("fake_quoted_data");
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_RSASSA,
      .signature.rsassa.sig = trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote"),
  };
  proxy_->GetFakeCrossystem().VbSetSystemPropertyString("hwid",
                                                        "fake_pcr_source_hint");

  auto load_key_result = LoadFakeRSAKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), ReadPCR(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kNonZeroPcr), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeQuotedStruct),
                      SetArgPointee<6>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  auto result = backend_->GetAttestationTpm2().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  ASSERT_TRUE(result->has_quoted_pcr_value());
  EXPECT_EQ(result->quoted_pcr_value(), kNonZeroPcr);
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_NE(result->quote().find("fake_quote"), std::string::npos);
  ASSERT_TRUE(result->has_pcr_source_hint());
  EXPECT_EQ(result->pcr_source_hint(), "fake_pcr_source_hint");
}

TEST_F(BackendAttestationTpm2Test, QuoteMultipleDeviceConfigs) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode, DeviceConfig::kCurrentUser};
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST("fake_quoted_data");
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_RSASSA,
      .signature.rsassa.sig = trunks::Make_TPM2B_PUBLIC_KEY_RSA("fake_quote"),
  };

  auto load_key_result = LoadFakeRSAKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(kFakeQuotedStruct),
                      SetArgPointee<6>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  auto result = backend_->GetAttestationTpm2().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_OK(result);
  EXPECT_FALSE(result->has_quoted_pcr_value());
  ASSERT_TRUE(result->has_quoted_data());
  EXPECT_EQ(result->quoted_data(), "fake_quoted_data");
  ASSERT_TRUE(result->has_quote());
  EXPECT_NE(result->quote().find("fake_quote"), std::string::npos);
  EXPECT_FALSE(result->has_pcr_source_hint());
}

TEST_F(BackendAttestationTpm2Test, QuoteFailure) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const std::string kNonZeroPcr(SHA256_DIGEST_LENGTH, 'X');
  const std::string kFakeKeyName = "fake_key_name";
  const uint32_t kFakeKeyHandle = 0x1337;

  auto load_key_result = LoadFakeRSAKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), ReadPCR(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kNonZeroPcr), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  auto result = backend_->GetAttestationTpm2().Quote(kFakeDeviceConfigs,
                                                     fake_key.GetKey());
  ASSERT_NOT_OK(result);
}

TEST_F(BackendAttestationTpm2Test, IsQuoted) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};

  auto pcr_selection_result =
      backend_->GetConfigTpm2().ToPcrSelection(kFakeDeviceConfigs);
  ASSERT_OK(pcr_selection_result);
  trunks::TPMS_PCR_SELECTION pcr_selection = pcr_selection_result.value();

  const trunks::TPMS_ATTEST fake_attest = {
      .magic = trunks::TPM_GENERATED_VALUE,
      .type = trunks::TPM_ST_ATTEST_QUOTE,
      .attested = {.quote = {.pcr_select = {
                                 .count = 1,
                                 .pcr_selections = {pcr_selection},
                             }}}};

  std::string serialized_fake_attest;
  EXPECT_EQ(trunks::Serialize_TPMS_ATTEST(fake_attest, &serialized_fake_attest),
            TPM_RC_SUCCESS);

  attestation::Quote fake_quote;
  fake_quote.set_quoted_data(serialized_fake_attest);

  auto is_quoted_result =
      backend_->GetAttestationTpm2().IsQuoted(kFakeDeviceConfigs, fake_quote);
  ASSERT_OK(is_quoted_result);
  EXPECT_TRUE(is_quoted_result.value());
}

TEST_F(BackendAttestationTpm2Test, IsQuotedWrontDeviceConfigs) {
  const DeviceConfigs kExpectedDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};
  const DeviceConfigs kQuotedDeviceConfigs =
      DeviceConfigs{DeviceConfig::kDeviceModel};

  auto pcr_selection_result =
      backend_->GetConfigTpm2().ToPcrSelection(kQuotedDeviceConfigs);
  ASSERT_OK(pcr_selection_result);
  trunks::TPMS_PCR_SELECTION pcr_selection = pcr_selection_result.value();

  const trunks::TPMS_ATTEST fake_attest = {
      .magic = trunks::TPM_GENERATED_VALUE,
      .type = trunks::TPM_ST_ATTEST_QUOTE,
      .attested = {.quote = {.pcr_select = {
                                 .count = 1,
                                 .pcr_selections = {pcr_selection},
                             }}}};

  std::string serialized_fake_attest;
  EXPECT_EQ(trunks::Serialize_TPMS_ATTEST(fake_attest, &serialized_fake_attest),
            TPM_RC_SUCCESS);

  attestation::Quote fake_quote;
  fake_quote.set_quoted_data(serialized_fake_attest);

  auto is_quoted_result = backend_->GetAttestationTpm2().IsQuoted(
      kExpectedDeviceConfigs, fake_quote);
  ASSERT_OK(is_quoted_result);
  EXPECT_FALSE(is_quoted_result.value());
}

TEST_F(BackendAttestationTpm2Test, IsQuotedWrongFormat) {
  const DeviceConfigs kFakeDeviceConfigs =
      DeviceConfigs{DeviceConfig::kBootMode};

  attestation::Quote fake_quote;
  fake_quote.set_quoted_data("");

  auto is_quoted_result =
      backend_->GetAttestationTpm2().IsQuoted(kFakeDeviceConfigs, fake_quote);
  ASSERT_NOT_OK(is_quoted_result);
}

TEST_F(BackendAttestationTpm2Test, CreateCertifiedKey) {
  const uint32_t kFakeIdentityHandle = 0x1773;
  const uint32_t kFakeKeyHandle = 0x1337;
  const size_t kFakeSize = 32;
  const brillo::SecureBlob kFakeData(kFakeSize, 'X');
  const std::string kFakeKeyName = "fake_key_name";
  const std::string kFakeIdentityName = "fake_identity_name";
  const std::string kFakeKeyBlob = "fake_key_blob";
  const trunks::TPMT_PUBLIC kFakePublic = kDefaultRsaPublic;
  const std::string kFakeCertifyInfoString = "fake_certify_info";
  const std::string kFakeSignatureString = "fake_signature";
  const trunks::TPM2B_ATTEST kFakeCertifyInfo =
      trunks::Make_TPM2B_ATTEST(kFakeCertifyInfoString);
  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_RSASSA,
      .signature.rsassa.sig =
          trunks::Make_TPM2B_PUBLIC_KEY_RSA(kFakeSignatureString),
  };
  const attestation::KeyType kFakeKeyType = attestation::KeyType::KEY_TYPE_RSA;
  const attestation::KeyUsage kFakeKeyUsage =
      attestation::KeyUsage::KEY_USAGE_DECRYPT;

  // Load the identity key.
  auto load_key_result = LoadFakeRSAKey(kFakeIdentityHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_identity_key = load_key_result.value();

  // Setup RandomSecureBlob
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              GenerateRandom(kFakeSize, nullptr, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeData.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  // Setup CreateKey
  EXPECT_CALL(
      proxy_->GetMockTpmUtility(),
      CreateRSAKeyPair(trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey, _,
                       _, _, _, false, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<8>(kFakeKeyBlob),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  // Setup CertifyKey
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeIdentityHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakeIdentityName), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpm(),
              CertifySync(kFakeKeyHandle, kFakeKeyName, kFakeIdentityHandle,
                          kFakeIdentityName, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(kFakeCertifyInfo),
                      SetArgPointee<7>(kFakeSignature),
                      Return(TPM_RC_SUCCESS)));

  std::string external_data = "external_data";
  auto result = backend_->GetAttestationTpm2().CreateCertifiedKey(
      fake_identity_key.GetKey(), kFakeKeyType, kFakeKeyUsage,
      KeyRestriction::kUnrestricted, EndorsementAuth::kEndorsement,
      external_data);

  std::string serialized_public_key;
  EXPECT_EQ(trunks::Serialize_TPMT_PUBLIC(kFakePublic, &serialized_public_key),
            TPM_RC_SUCCESS);

  ASSERT_OK(result);
  ASSERT_TRUE(result->has_key_blob());
  ASSERT_TRUE(result->has_public_key());
  ASSERT_TRUE(result->has_public_key_tpm_format());
  ASSERT_TRUE(result->has_certified_key_info());
  ASSERT_TRUE(result->has_certified_key_proof());
  ASSERT_TRUE(result->has_key_type());
  ASSERT_TRUE(result->has_key_usage());
  EXPECT_EQ(result->key_blob(), kFakeKeyBlob);
  EXPECT_EQ(result->public_key_tpm_format(), serialized_public_key);
  EXPECT_EQ(result->certified_key_info(), kFakeCertifyInfoString);
  EXPECT_EQ(result->certified_key_proof(), kFakeSignatureString);
  EXPECT_EQ(result->key_type(), kFakeKeyType);
  EXPECT_EQ(result->key_usage(), kFakeKeyUsage);
}

TEST_F(BackendAttestationTpm2Test, CreateIdentity) {
  const attestation::KeyType kFakeKeyType = attestation::KEY_TYPE_RSA;
  const trunks::TPM_ALG_ID kFakeTrunksAlgorithm = trunks::TPM_ALG_RSA;
  const std::string kFakeKeyBlob = "fake_key_blob";
  const trunks::TPM2B_PUBLIC kFakePublic{
      .size = sizeof(trunks::TPMT_PUBLIC),
      .public_area = kDefaultRsaPublic,
  };

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              CreateIdentityKey(kFakeTrunksAlgorithm, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyBlob), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockBlobParser(), ParseKeyBlob(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakePublic), Return(true)));

  std::string serialized_public_key;
  EXPECT_EQ(
      trunks::Serialize_TPMT_PUBLIC(kDefaultRsaPublic, &serialized_public_key),
      TPM_RC_SUCCESS);

  auto result = backend_->GetAttestationTpm2().CreateIdentity(kFakeKeyType);
  ASSERT_OK(result);
  attestation::IdentityKey identity_key = result->identity_key;
  attestation::IdentityBinding identity_binding = result->identity_binding;
  ASSERT_TRUE(identity_key.has_identity_key_type());
  ASSERT_TRUE(identity_key.has_identity_public_key_der());
  ASSERT_TRUE(identity_key.has_identity_key_blob());
  ASSERT_TRUE(identity_binding.has_identity_public_key_tpm_format());
  ASSERT_TRUE(identity_binding.has_identity_public_key_der());
  EXPECT_EQ(identity_key.identity_key_type(), kFakeKeyType);
  EXPECT_EQ(identity_key.identity_key_blob(), kFakeKeyBlob);
  EXPECT_EQ(identity_binding.identity_public_key_tpm_format(),
            serialized_public_key);
}

}  // namespace hwsec
