// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <trunks/mock_tpm_utility.h>

#include "base/hash/sha1.h"
#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "trunks/tpm_generated.h"

// Prevent the conflict definition from tss.h
#undef TPM_ALG_RSA

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::NotOkWith;
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

using BackendSigningTpm2Test = BackendTpm2TestBase;

TEST_F(BackendSigningTpm2Test, SignRSA) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_RSASSA,
                   trunks::TPM_ALG_SHA256, kDataToSign, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(
      backend_->GetSigningTpm2().RawSign(
          key->GetKey(), brillo::BlobFromString(kDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kSha256,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

TEST_F(BackendSigningTpm2Test, SignECCSha1Raw) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToHash = "data_to_sign";
  const std::string kDataToSign = base::SHA1HashString(kDataToHash);
  trunks::TPMT_SIGNATURE fake_signature;
  const std::string kSignatureR = "signature_r";
  const std::string kSignatureS = "signature_s";
  fake_signature.sig_alg = trunks::TPM_ALG_ECDSA;
  fake_signature.signature.ecdsa.signature_r =
      trunks::Make_TPM2B_ECC_PARAMETER(kSignatureR);
  fake_signature.signature.ecdsa.signature_s =
      trunks::Make_TPM2B_ECC_PARAMETER(kSignatureS);
  std::string kSignature;
  ASSERT_EQ(Serialize_TPMT_SIGNATURE(fake_signature, &kSignature),
            trunks::TPM_RC_SUCCESS);
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_ECC,
      .name_alg = trunks::TPM_ALG_SHA1,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_ECDSA, trunks::TPM_ALG_SHA1,
                   kDataToSign, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kNoDigest,
                  }),
              IsOkAndHolds(brillo::BlobFromString(kSignatureR + kSignatureS)));
}

TEST_F(BackendSigningTpm2Test, SignECC) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  trunks::TPMT_SIGNATURE fake_signature;
  const std::string kSignatureR = "signature_r";
  const std::string kSignatureS = "signature_s";
  fake_signature.sig_alg = trunks::TPM_ALG_ECDSA;
  fake_signature.signature.ecdsa.signature_r =
      trunks::Make_TPM2B_ECC_PARAMETER(kSignatureR);
  fake_signature.signature.ecdsa.signature_s =
      trunks::Make_TPM2B_ECC_PARAMETER(kSignatureS);
  std::string kSignature;
  ASSERT_EQ(Serialize_TPMT_SIGNATURE(fake_signature, &kSignature),
            trunks::TPM_RC_SUCCESS);
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_ECC,
      .name_alg = trunks::TPM_ALG_SHA1,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_ECDSA, trunks::TPM_ALG_SHA1,
                   kDataToSign, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                  }),
              IsOkAndHolds(brillo::BlobFromString(kSignatureR + kSignatureS)));
}

TEST_F(BackendSigningTpm2Test, SignECCWrongResponse) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "wrong_signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_ECC,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_ECDSA,
                   trunks::TPM_ALG_SHA256, kDataToSign, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha256,
                  }),
              NotOk());
}

TEST_F(BackendSigningTpm2Test, SignUnknownKey) {
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(
      backend_->GetSigningTpm2().Sign(
          key->GetKey(), brillo::BlobFromString(kDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kSha256,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      NotOkWith("Unknown TPM key type"));
}

TEST_F(BackendSigningTpm2Test, SignRSAPkcs1v15WithNull) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_NULL,
                   _, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(
      backend_->GetSigningTpm2().RawSign(
          key->GetKey(), brillo::BlobFromString(kDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kMd5,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

TEST_F(BackendSigningTpm2Test, SignRSARsassaPss) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_RSAPSS,
                   trunks::TPM_ALG_SHA512, kDataToSign, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha512,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

TEST_F(BackendSigningTpm2Test, SignRSARsassaPssUnsupported) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kMd5,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("Unsupported digest algorithm combination"));
}

TEST_F(BackendSigningTpm2Test, SignRSAPkcs1v15WithDecrypt) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent |
                           trunks::kSign | trunks::kDecrypt,
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
                      .size = 64,
                      .buffer = "9876543210987654321098765432109876543210987654"
                                "321098765432104321",
                  },
          },
  };

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              AsymmetricDecrypt(kFakeKeyHandle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(
      backend_->GetSigningTpm2().RawSign(
          key->GetKey(), brillo::BlobFromString(kDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kSha384,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

TEST_F(BackendSigningTpm2Test, SignRSAPkcs1v15WithDecryptTooLong) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data_to_sign";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent |
                           trunks::kSign | trunks::kDecrypt,
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
                      .size = 32,
                      .buffer = "98765432109876543210987654321012",
                  },
          },
  };

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(
      backend_->GetSigningTpm2().RawSign(
          key->GetKey(), brillo::BlobFromString(kDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kSha384,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      NotOkWith("Message too long"));
}

TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecrypt) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(20, 'D');
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent |
                           trunks::kSign | trunks::kDecrypt,
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
                      .size = 64,
                      .buffer = "9876543210987654321098765432109876543210987654"
                                "321098765432104321",
                  },
          },
  };

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              AsymmetricDecrypt(kFakeKeyHandle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptDataTooSmall) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = "data";
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent |
                           trunks::kSign | trunks::kDecrypt,
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
                      .size = 64,
                      .buffer = "9876543210987654321098765432109876543210987654"
                                "321098765432104321",
                  },
          },
  };

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("Data to sign is too small"));
}

namespace {

// Public area shared by the RSASSA-PSS pre-condition tests below, which differ
// only in their SigningOptions. |modulus_size| is the length of
// |unique.rsa.size|: the software padding path sizes the encoded message from
// that field, not from |key_bits|, and the salt-length boundary cases need to
// vary it.
trunks::TPMT_PUBLIC GetRsaPssTestPublicArea(uint16_t modulus_size = 64) {
  trunks::TPMT_PUBLIC public_area = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes = trunks::kFixedTPM | trunks::kFixedParent |
                           trunks::kSign | trunks::kDecrypt,
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
                      .size = modulus_size,
                      .buffer = "9876543210987654321098765432109876543210987654"
                                "3210987654321043219876543210987654321098765432"
                                "10987654321098765432104321",
                  },
          },
  };
  return public_area;
}

}  // namespace

TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptUnsupportedMgf1Alg) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(20, 'D');
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                      .pss_params =
                          SigningOptions::PssParams{
                              .mgf1_algorithm = DigestAlgorithm::kNoDigest,
                              .salt_length = 1024,
                          },
                  }),
              NotOkWith("Unsupported digest algorithm for PSS"));
}

// An input longer than one digest used to be accepted: OpenSSL silently
// consumed only the first hLen bytes, so the caller received a signature over
// a truncated message. Chaps rejects this, and libhwsec now matches.
TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptDataTooLarge) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(21, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("Data to sign is too small or too large"));
}

// GetDigestLength(kNoDigest) is 0 and GetOpenSSLDigest(kNoDigest) is nullptr,
// so PSS without a digest algorithm must be rejected outright rather than
// reaching RSA_padding_add_PKCS1_PSS_mgf1() with a null EVP_MD.
TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptNoDigestAlgorithm) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(20, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kNoDigest,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("PSS signing requires a digest algorithm"));

  // The same request with an empty input must also be rejected: an equality
  // check alone would let 0 == GetDigestLength(kNoDigest) through.
  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto empty_key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(empty_key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  empty_key->GetKey(), brillo::Blob(),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kNoDigest,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("PSS signing requires a digest algorithm"));
}

// The default salt length is |modulus| - hLen - 2 computed in size_t. A
// modulus too small for the digest used to underflow into the OpenSSL
// sentinels RSA_PSS_SALTLEN_DIGEST (-1) / RSA_PSS_SALTLEN_AUTO (-2).
TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptKeyTooSmall) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(64, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  // 64-byte modulus, 64-byte SHA-512 digest: |modulus| - hLen - 2 underflows.
  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha512,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("RSA key too small for PSS with this digest"));
}

// A caller-supplied salt that does not fit the modulus is still rejected by
// OpenSSL. This keeps coverage of that path, which the UnsupportedMgf1Alg test
// above no longer reaches now that the null EVP_MD is caught earlier.
TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptSaltTooLarge) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(20, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                      .pss_params =
                          SigningOptions::PssParams{
                              .mgf1_algorithm = DigestAlgorithm::kSha1,
                              .salt_length = 1024,
                          },
                  }),
              NotOkWith("Failed to produce the PSA PSS paddings"));
}

// PssParams::salt_length is a size_t narrowed to an int at the OpenSSL call.
// A value above INT_MAX used to land on RSA_PSS_SALTLEN_DIGEST (-1), which
// OpenSSL accepts: the request SUCCEEDED with a salt length the caller never
// asked for, rather than failing.
TEST_F(BackendSigningTpm2Test, SignRSARsassaPssWithDecryptSaltOutOfRange) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(20, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea();

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha1,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                      .pss_params =
                          SigningOptions::PssParams{
                              .mgf1_algorithm = DigestAlgorithm::kSha1,
                              .salt_length = std::numeric_limits<size_t>::max(),
                          },
                  }),
              NotOkWith("PSS salt length out of range"));
}

// The other underflow sentinel. A modulus of exactly hLen + 1 makes the
// default salt computation produce (size_t)-1, which narrows to
// RSA_PSS_SALTLEN_DIGEST. Unlike the -2 case this one does NOT error in
// OpenSSL: it silently substitutes sLen = hLen.
TEST_F(BackendSigningTpm2Test,
       SignRSARsassaPssWithDecryptSaltLenDigestSentinel) {
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign(64, 'D');
  const uint32_t kFakeKeyHandle = 0x1337;
  // 65-byte modulus with a 64-byte SHA-512 digest: 65 - 64 - 2 == (size_t)-1.
  const trunks::TPMT_PUBLIC kFakePublic = GetRsaPssTestPublicArea(65);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_THAT(backend_->GetSigningTpm2().RawSign(
                  key->GetKey(), brillo::BlobFromString(kDataToSign),
                  SigningOptions{
                      .digest_algorithm = DigestAlgorithm::kSha512,
                      .rsa_padding_scheme =
                          SigningOptions::RsaPaddingScheme::kRsassaPss,
                  }),
              NotOkWith("RSA key too small for PSS with this digest"));
}

TEST_F(BackendSigningTpm2Test, SignRSAPkcs1v15WithoutDigestAlgorithm) {
  constexpr uint8_t kSha512DigestInfo[] = {
      0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
  const OperationPolicy kFakePolicy{};
  const std::string kFakeKeyBlob = "fake_key_blob";
  const std::string kDataToSign = std::string(64, 'X');
  const std::string kFullDataToSign =
      std::string(kSha512DigestInfo,
                  kSha512DigestInfo + sizeof(kSha512DigestInfo)) +
      kDataToSign;
  const std::string kSignature = "signature";
  const uint32_t kFakeKeyHandle = 0x1337;
  const trunks::TPMT_PUBLIC kFakePublic = {
      .type = trunks::TPM_ALG_RSA,
      .name_alg = trunks::TPM_ALG_SHA256,
      .object_attributes =
          trunks::kFixedTPM | trunks::kFixedParent | trunks::kSign,
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

  EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kFakeKeyHandle),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyPublicArea(kFakeKeyHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFakePublic), Return(trunks::TPM_RC_SUCCESS)));

  auto key = backend_->GetKeyManagementTpm2().LoadKey(
      kFakePolicy, brillo::BlobFromString(kFakeKeyBlob),
      Backend::KeyManagement::LoadKeyOptions{});

  ASSERT_OK(key);

  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              Sign(kFakeKeyHandle, trunks::TPM_ALG_RSASSA,
                   trunks::TPM_ALG_SHA512, _, false, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(kSignature), Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(
      backend_->GetSigningTpm2().RawSign(
          key->GetKey(), brillo::BlobFromString(kFullDataToSign),
          SigningOptions{
              .digest_algorithm = DigestAlgorithm::kNoDigest,
              .rsa_padding_scheme = SigningOptions::RsaPaddingScheme::kPkcs1v15,
          }),
      IsOkAndHolds(brillo::BlobFromString(kSignature)));
}

}  // namespace hwsec
