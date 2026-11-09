// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <trunks/error_codes.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/structures/signature_sealed_data_test_utils.h"

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

namespace hwsec {

namespace {

using Algorithm = Backend::SignatureSealing::Algorithm;

constexpr trunks::TPM_RC kRmHandleError =
    trunks::TPM_RC_HANDLE | trunks::kResourceManagerTpmErrorBase;

bool GenerateRsaKey(int key_size_bits,
                    crypto::ScopedEVP_PKEY* pkey,
                    brillo::Blob* key_spki_der) {
  crypto::ScopedEVP_PKEY_CTX pkey_context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  if (!pkey_context) {
    return false;
  }
  if (EVP_PKEY_keygen_init(pkey_context.get()) <= 0) {
    return false;
  }
  if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_context.get(), key_size_bits) <=
      0) {
    return false;
  }
  EVP_PKEY* pkey_raw = nullptr;
  if (EVP_PKEY_keygen(pkey_context.get(), &pkey_raw) <= 0) {
    return false;
  }
  pkey->reset(pkey_raw);
  // Obtain the DER-encoded Subject Public Key Info.
  const int key_spki_der_length = i2d_PUBKEY(pkey->get(), nullptr);
  if (key_spki_der_length < 0) {
    return false;
  }
  key_spki_der->resize(key_spki_der_length);
  unsigned char* key_spki_der_buffer = key_spki_der->data();
  return i2d_PUBKEY(pkey->get(), &key_spki_der_buffer) == key_spki_der->size();
}

}  // namespace

class BackendSignatureSealingTpm2Test : public BackendTpm2TestBase {
 protected:
  BackendSignatureSealingTpm2Test()
      : current_user_("username"),
        unsealed_data_("secret data"),
        trunks_sealed_data_("trunks sealed data"),
        fake_digests1_(SHA256_DIGEST_LENGTH, 'A'),
        fake_digests2_(SHA256_DIGEST_LENGTH, 'B'),
        fake_digests3_(SHA256_DIGEST_LENGTH, 'X'),
        fake_tpm_nonce_("TPM nounce"),
        fake_key_name_("key name"),
        fake_challenge_response_("challenge response"),
        key_algorithms_({
            Algorithm::kRsassaPkcs1V15Sha1,
            Algorithm::kRsassaPkcs1V15Sha256,
        }),
        operation_policy_setting_({
            OperationPolicySetting{
                .device_config_settings =
                    DeviceConfigSettings{
                        .current_user =
                            DeviceConfigSettings::CurrentUserSetting{
                                .username = std::nullopt}}},
            OperationPolicySetting{
                .device_config_settings =
                    DeviceConfigSettings{
                        .current_user =
                            DeviceConfigSettings::CurrentUserSetting{
                                .username = current_user_}}},
        }),
        operation_policy_(OperationPolicy{
            .device_configs = DeviceConfigs{DeviceConfig::kCurrentUser},
        }) {
    EXPECT_TRUE(GenerateRsaKey(2048, &pkey_, &public_key_spki_der_));
  }

  void SetUp() override {
    BackendTpm2TestBase::SetUp();
    EXPECT_CALL(proxy_->GetMockTpmState(), GetAlgorithmProperties(_, nullptr))
        .WillRepeatedly(Return(true));
  }

  StatusOr<SignatureSealedData> SetupSealing() {
    // ConfigTpm2::GetPolicyDigest twice for two policy settings.
    EXPECT_CALL(proxy_->GetMockTrialSession(),
                StartUnboundSession(false, false))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));
    EXPECT_CALL(proxy_->GetMockTrialSession(), PolicyPCR(_))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));

    EXPECT_CALL(proxy_->GetMockTrialSession(), GetDigest(_))
        // The intermediate digests for different policy.
        .WillOnce(DoAll(SetArgPointee<0>(fake_digests1_),
                        Return(trunks::TPM_RC_SUCCESS)))
        .WillOnce(DoAll(SetArgPointee<0>(fake_digests2_),
                        Return(trunks::TPM_RC_SUCCESS)))
        // The final digest that combines intermediate digests and signature.
        .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                        Return(trunks::TPM_RC_SUCCESS)));

    EXPECT_CALL(proxy_->GetMockTrialSession(), StartUnboundSession(true, false))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));
    EXPECT_CALL(
        proxy_->GetMockTrialSession(),
        PolicyOR(std::vector<std::string>{fake_digests1_, fake_digests2_}))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));
    EXPECT_CALL(proxy_->GetMockTrialSession(),
                PolicySigned(_, _, _, _, _, _, _, _))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));

    // Seal the data.
    EXPECT_CALL(
        proxy_->GetMockTpmUtility(),
        SealData(unsealed_data_.to_string(), fake_digests3_, "", true, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(trunks_sealed_data_),
                        Return(trunks::TPM_RC_SUCCESS)));

    return backend_->GetSignatureSealingTpm2().Seal(
        operation_policy_setting_, unsealed_data_, public_key_spki_der_,
        key_algorithms_);
  }

  StatusOr<Backend::SignatureSealing::ChallengeResult> SetupChallenge(
      const SignatureSealedData& sealed_data) {
    EXPECT_CALL(proxy_->GetMockPolicySession(),
                StartUnboundSession(true, false))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));

    EXPECT_CALL(proxy_->GetMockPolicySession(), PolicyAuthValue()).Times(0);

    EXPECT_CALL(proxy_->GetMockPolicySession(), PolicyPCR(_))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));

    EXPECT_CALL(
        proxy_->GetMockPolicySession(),
        PolicyOR(std::vector<std::string>{fake_digests1_, fake_digests2_}))
        .WillOnce(Return(trunks::TPM_RC_SUCCESS));

    trunks::MockAuthorizationDelegate authorization_delegate;

    EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
        .WillOnce(Return(&authorization_delegate));
    EXPECT_CALL(authorization_delegate, GetTpmNonce(_))
        .WillOnce(DoAll(SetArgPointee<0>(fake_tpm_nonce_), Return(true)));

    return backend_->GetSignatureSealingTpm2().Challenge(
        operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  }

  // Default parameters.
  std::string current_user_;
  brillo::SecureBlob unsealed_data_;
  std::string trunks_sealed_data_;
  std::string fake_digests1_;
  std::string fake_digests2_;
  std::string fake_digests3_;
  std::string fake_tpm_nonce_;
  std::string fake_key_name_;
  std::string fake_challenge_response_;
  crypto::ScopedEVP_PKEY pkey_;
  brillo::Blob public_key_spki_der_;
  std::vector<Algorithm> key_algorithms_;
  std::vector<OperationPolicySetting> operation_policy_setting_;
  OperationPolicy operation_policy_;
};

TEST_F(BackendSignatureSealingTpm2Test, SealChallengeUnseal) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);
  SignatureSealedData expected_seal_result = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA256,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };
  EXPECT_EQ(seal_result.value(), expected_seal_result);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result =
      SetupChallenge(seal_result.value());

  ASSERT_OK(challenge_result);
  EXPECT_EQ(challenge_result->algorithm, Algorithm::kRsassaPkcs1V15Sha256);

  brillo::Blob challenge_value = brillo::CombineBlobs(
      {brillo::BlobFromString(fake_tpm_nonce_), brillo::Blob(4)});
  EXPECT_EQ(challenge_result->challenge, challenge_value);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockPolicySession(),
              PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, _))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));

  trunks::MockAuthorizationDelegate delegate;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate));

  // Unseal the data.
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(backend_->GetSignatureSealingTpm2().Unseal(
                  challenge_result->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));
}

TEST_F(BackendSignatureSealingTpm2Test, SealWithSha1) {
  key_algorithms_ = std::vector<Algorithm>{
      Algorithm::kRsassaPkcs1V15Sha1,
  };

  SignatureSealedData expected_seal_result = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA1,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };

  EXPECT_THAT(SetupSealing(), IsOkAndHolds(expected_seal_result));
}

TEST_F(BackendSignatureSealingTpm2Test, SealAlgorithmPriority) {
  key_algorithms_ = std::vector<Algorithm>{
      Algorithm::kRsassaPkcs1V15Sha512,
      Algorithm::kRsassaPkcs1V15Sha256,
      Algorithm::kRsassaPkcs1V15Sha384,
      Algorithm::kRsassaPkcs1V15Sha1,
  };

  SignatureSealedData expected_seal_result = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA512,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };

  EXPECT_THAT(SetupSealing(), IsOkAndHolds(expected_seal_result));
}

TEST_F(BackendSignatureSealingTpm2Test, SealAlgorithmPriorityReverse) {
  key_algorithms_ = std::vector<Algorithm>{
      Algorithm::kRsassaPkcs1V15Sha1,
      Algorithm::kRsassaPkcs1V15Sha384,
      Algorithm::kRsassaPkcs1V15Sha256,
      Algorithm::kRsassaPkcs1V15Sha512,
  };

  SignatureSealedData expected_seal_result = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA384,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };

  EXPECT_THAT(SetupSealing(), IsOkAndHolds(expected_seal_result));
}

TEST_F(BackendSignatureSealingTpm2Test, SealAlgorithmNoSha512) {
  EXPECT_CALL(proxy_->GetMockTpmState(),
              GetAlgorithmProperties(trunks::TPM_ALG_SHA512, nullptr))
      .WillRepeatedly(Return(false));

  key_algorithms_ = std::vector<Algorithm>{
      Algorithm::kRsassaPkcs1V15Sha512,
      Algorithm::kRsassaPkcs1V15Sha256,
      Algorithm::kRsassaPkcs1V15Sha384,
      Algorithm::kRsassaPkcs1V15Sha1,
  };

  SignatureSealedData expected_seal_result = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA256,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };

  EXPECT_THAT(SetupSealing(), IsOkAndHolds(expected_seal_result));
}

TEST_F(BackendSignatureSealingTpm2Test, SealAlgorithmNoRsassa) {
  EXPECT_CALL(proxy_->GetMockTpmState(),
              GetAlgorithmProperties(trunks::TPM_ALG_RSASSA, nullptr))
      .WillRepeatedly(Return(false));

  key_algorithms_ = std::vector<Algorithm>{
      Algorithm::kRsassaPkcs1V15Sha512,
      Algorithm::kRsassaPkcs1V15Sha256,
      Algorithm::kRsassaPkcs1V15Sha384,
      Algorithm::kRsassaPkcs1V15Sha1,
  };

  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      operation_policy_setting_, unsealed_data_, public_key_spki_der_,
      key_algorithms_);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, SealNoPubKey) {
  brillo::Blob public_key_spki_der = brillo::BlobFromString("Wrong format key");

  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      operation_policy_setting_, unsealed_data_, public_key_spki_der,
      key_algorithms_);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, SealNoAlgorithm) {
  std::vector<Algorithm> key_algorithms{};

  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      operation_policy_setting_, unsealed_data_, public_key_spki_der_,
      key_algorithms);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, SealNoSetting) {
  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      std::vector<OperationPolicySetting>{}, unsealed_data_,
      public_key_spki_der_, key_algorithms_);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, SealWrongSetting) {
  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      std::vector<OperationPolicySetting>{
          OperationPolicySetting{
              .device_config_settings =
                  DeviceConfigSettings{
                      .current_user =
                          DeviceConfigSettings::CurrentUserSetting{
                              .username = std::nullopt}},
              .permission =
                  Permission{.auth_value = brillo::SecureBlob("value")},
          },
          OperationPolicySetting{
              .device_config_settings =
                  DeviceConfigSettings{
                      .current_user =
                          DeviceConfigSettings::CurrentUserSetting{
                              .username = current_user_}},
          },
      },
      unsealed_data_, public_key_spki_der_, key_algorithms_);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, SealWrongDigestLength) {
  std::string fake_digests3 = "";

  // ConfigTpm2::GetPolicyDigest twice for two policy settings.
  EXPECT_CALL(proxy_->GetMockTrialSession(), StartUnboundSession(false, false))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockTrialSession(), PolicyPCR(_))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  EXPECT_CALL(proxy_->GetMockTrialSession(), GetDigest(_))
      // The intermediate digests for different policy.
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests1_),
                      Return(trunks::TPM_RC_SUCCESS)))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests2_),
                      Return(trunks::TPM_RC_SUCCESS)))
      // The final digest that combines intermediate digests and signature.
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(proxy_->GetMockTrialSession(), StartUnboundSession(true, false))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(
      proxy_->GetMockTrialSession(),
      PolicyOR(std::vector<std::string>{fake_digests1_, fake_digests2_}))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));

  auto seal_result = backend_->GetSignatureSealingTpm2().Seal(
      operation_policy_setting_, unsealed_data_, public_key_spki_der_,
      key_algorithms_);

  EXPECT_FALSE(seal_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, ChallengeWrongData) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  // Wrong method.
  SignatureSealedData sealed_data = Tpm12CertifiedMigratableKeyData{};
  auto challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Empty public_key_spki_der.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).public_key_spki_der =
      brillo::Blob();
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Empty srk_wrapped_secret.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).srk_wrapped_secret =
      brillo::Blob();
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Empty scheme.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).scheme = std::nullopt;
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Empty hash_alg.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).hash_alg = std::nullopt;
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Empty policy digest.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).pcr_policy_digests[0].digest =
      brillo::Blob();
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Wrong policy digest size.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).pcr_policy_digests[0].digest =
      brillo::Blob(16, 'A');
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Mismatch public_key_spki_der.
  sealed_data = seal_result.value();
  std::get<Tpm2PolicySignedData>(sealed_data).public_key_spki_der[0] =
      ~std::get<Tpm2PolicySignedData>(sealed_data).public_key_spki_der[0];
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, sealed_data, public_key_spki_der_, key_algorithms_);
  EXPECT_FALSE(challenge_result.ok());

  // Mismatch algorithm.
  std::vector<Algorithm> key_algorithms{
      Algorithm::kRsassaPkcs1V15Sha384,
      Algorithm::kRsassaPkcs1V15Sha512,
  };
  challenge_result = backend_->GetSignatureSealingTpm2().Challenge(
      operation_policy_, seal_result.value(), public_key_spki_der_,
      key_algorithms);
  EXPECT_FALSE(challenge_result.ok());
}

TEST_F(BackendSignatureSealingTpm2Test, UnsealWrongData) {
  // No challenge.
  auto unseal_result = backend_->GetSignatureSealingTpm2().Unseal(
      static_cast<Backend::SignatureSealing::ChallengeID>(0),
      brillo::BlobFromString(fake_challenge_response_));
  EXPECT_FALSE(unseal_result.ok());

  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result =
      SetupChallenge(seal_result.value());

  ASSERT_OK(challenge_result);

  // Wrong challenge ID.
  Backend::SignatureSealing::ChallengeID challenge_id =
      challenge_result->challenge_id;
  challenge_id = static_cast<Backend::SignatureSealing::ChallengeID>(
      static_cast<uint32_t>(challenge_id) + 3);
  EXPECT_THAT(
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_id, brillo::BlobFromString(fake_challenge_response_)),
      NotOk());
}

TEST_F(BackendSignatureSealingTpm2Test,
       UnsealPassesNullDelegateWithoutStartingHmacSession) {
  // Construct |sealed_data| directly instead of calling SetupSealing(), which
  // starts an HMAC session during Seal().
  SignatureSealedData sealed_data = Tpm2PolicySignedData{
      .public_key_spki_der = public_key_spki_der_,
      .srk_wrapped_secret = brillo::BlobFromString(trunks_sealed_data_),
      .scheme = trunks::TPM_ALG_RSASSA,
      .hash_alg = trunks::TPM_ALG_SHA256,
      .pcr_policy_digests =
          {
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests1_)},
              Tpm2PolicyDigest{.digest =
                                   brillo::BlobFromString(fake_digests2_)},
          },
  };

  EXPECT_CALL(proxy_->GetMockHmacSession(), StartUnboundSession(_, _)).Times(0);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result =
      SetupChallenge(sealed_data);
  ASSERT_OK(challenge_result);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate unseal_delegate;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&unseal_delegate));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &unseal_delegate, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  Middleware middleware(middleware_owner_->Derive());
  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  challenge_result->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));
}

TEST_F(BackendSignatureSealingTpm2Test, ChallengeDropsPriorActiveChallenge) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result1 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result1);

  // Case 1: If starting the new policy session fails in a subsequent
  // Challenge(), the prior challenge was already dropped before starting the
  // session, and Middleware stops with kLater after flushing SetupSealing()'s
  // cached HMAC session once.
  EXPECT_CALL(proxy_->GetMockHmacSession(), StartUnboundSession(_, _)).Times(0);
  EXPECT_CALL(proxy_->GetMockPolicySession(), StartUnboundSession(true, false))
      .Times(2)
      .WillRepeatedly(Return(kRmHandleError));

  Middleware middleware(middleware_owner_->Derive());
  StatusOr<Backend::SignatureSealing::ChallengeResult> failed_challenge =
      middleware.CallSync<&Backend::SignatureSealing::Challenge>(
          operation_policy_, seal_result.value(), public_key_spki_der_,
          key_algorithms_);
  ASSERT_NOT_OK(failed_challenge);
  EXPECT_EQ(failed_challenge.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kLater);

  StatusOr<brillo::SecureBlob> stale_unseal =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result1->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(stale_unseal);
  EXPECT_EQ(stale_unseal.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);

  // Case 2: When a challenge is active and a subsequent Challenge() succeeds,
  // the previous challenge ID is superseded and the new challenge ID unseals.
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_a =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_a);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_b =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_b);
  ASSERT_NE(challenge_a->challenge_id, challenge_b->challenge_id);

  StatusOr<brillo::SecureBlob> superseded_unseal =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_a->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(superseded_unseal);
  EXPECT_EQ(superseded_unseal.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  challenge_b->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));
}

TEST_F(BackendSignatureSealingTpm2Test,
       UnsealDropsLostChallengeSessionAndPreservesRetryAction) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result);

  // Even though SetupSealing() left a cached HMAC session in
  // SessionManagementTpm2, a lost challenge policy session (0xb08b) in
  // PolicySigned drops the challenge and flushes cached HMAC sessions
  // immediately so Middleware returns kLater on the first attempt instead of
  // re-entering Unseal() and returning kNoRetry ("No valid challenge data").
  EXPECT_CALL(proxy_->GetMockHmacSession(), StartUnboundSession(_, _)).Times(0);
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(kRmHandleError));

  Middleware middleware(middleware_owner_->Derive());
  StatusOr<brillo::SecureBlob> unseal_result =
      middleware.CallSync<&Backend::SignatureSealing::Unseal>(
          challenge_result->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(unseal_result);
  EXPECT_EQ(unseal_result.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kLater);
  EXPECT_THAT(backend_->GetSessionManagementTpm2().FlushInvalidSessions(),
              NotOk());

  StatusOr<brillo::SecureBlob> evicted_retry =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(evicted_retry);
  EXPECT_EQ(evicted_retry.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);

  // Subsequent Challenge() + Unseal() round-trip recovers cleanly.
  StatusOr<Backend::SignatureSealing::ChallengeResult> retry_challenge =
      SetupChallenge(seal_result.value());
  ASSERT_OK(retry_challenge);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  retry_challenge->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));
}

TEST_F(BackendSignatureSealingTpm2Test, UnsealOnTransientRetryError) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  // Case 1: Transient TPM_RC_RETRY (0x922) in PolicySigned does not mean the
  // challenge session is gone; the challenge survives and succeeds on in-place
  // retry. TPMRetryHandler retries kLater only if FlushInvalidSessions()
  // succeeds, which here relies on the HMAC session cached by SetupSealing().
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result1 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result1);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_key_name_),
                            Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_RETRY))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate1;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate1));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate1, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  Middleware middleware(middleware_owner_->Derive());
  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  challenge_result1->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));

  // Case 2: Transient TPM_RC_RETRY in GetDigest after PolicySigned succeeded
  // keeps the already-extended challenge session and succeeds on in-place retry
  // without re-applying PolicySigned.
  EXPECT_CALL(proxy_->GetMockHmacSession(), StartUnboundSession(true, true))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  ASSERT_THAT(backend_->GetSessionManagementTpm2().GetOrCreateHmacSession(
                  SessionSecuritySetting::kSaltAndEncrypted),
              IsOk());

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result2 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result2);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(Return(trunks::TPM_RC_RETRY))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate2;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate2));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate2, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  challenge_result2->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));
}

TEST_F(BackendSignatureSealingTpm2Test,
       UnsealPostPolicySignedCommunicationRetryAndResponseMismatch) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result);

  // If UnsealData fails once with a communication error after PolicySigned has
  // already succeeded, TPMRetryHandler retries without calling PolicySigned a
  // second time on the already-extended policy session.
  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .Times(2)
      .WillRepeatedly(DoAll(SetArgPointee<0>(fake_digests3_),
                            Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .Times(2)
      .WillRepeatedly(Return(&delegate));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate, _))
      .WillOnce(Return(trunks::SAPI_RC_NO_RESPONSE_RECEIVED))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data_.to_string()),
                      Return(trunks::TPM_RC_SUCCESS)));

  Middleware middleware(middleware_owner_->Derive());
  EXPECT_THAT(middleware.CallSync<&Backend::SignatureSealing::Unseal>(
                  challenge_result->challenge_id,
                  brillo::BlobFromString(fake_challenge_response_)),
              IsOkAndHolds(unsealed_data_));

  // If GetDigest fails with a communication error after PolicySigned has
  // already succeeded, a subsequent Unseal() carrying a different
  // challenge_response must be rejected rather than skipping PolicySigned.
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result2 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result2);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(Return(trunks::SAPI_RC_NO_RESPONSE_RECEIVED));

  StatusOr<brillo::SecureBlob> comm_fail =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result2->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(comm_fail);
  EXPECT_EQ(comm_fail.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kCommunication);

  StatusOr<brillo::SecureBlob> mismatch_unseal =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result2->challenge_id,
          brillo::BlobFromString("different challenge response"));
  ASSERT_NOT_OK(mismatch_unseal);
  EXPECT_EQ(mismatch_unseal.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);
  StatusOr<brillo::SecureBlob> cleared_after_mismatch =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result2->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(cleared_after_mismatch);
  EXPECT_EQ(cleared_after_mismatch.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);
}

TEST_F(BackendSignatureSealingTpm2Test,
       UnsealStopsRetryingOnPostPolicySignedErrors) {
  StatusOr<SignatureSealedData> seal_result = SetupSealing();
  ASSERT_OK(seal_result);

  // Case 1: GetDigest fails with kRmHandleError (kLater) -> drops challenge and
  // cached HMAC sessions immediately without retrying.
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result1 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result1);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(Return(kRmHandleError));

  Middleware middleware(middleware_owner_->Derive());
  StatusOr<brillo::SecureBlob> unseal_result1 =
      middleware.CallSync<&Backend::SignatureSealing::Unseal>(
          challenge_result1->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(unseal_result1);
  EXPECT_EQ(unseal_result1.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kLater);
  EXPECT_THAT(backend_->GetSessionManagementTpm2().FlushInvalidSessions(),
              NotOk());
  StatusOr<brillo::SecureBlob> cleared_check1 =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result1->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(cleared_check1);
  EXPECT_EQ(cleared_check1.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);

  // Case 2: UnsealData fails with TPM_RC_REFERENCE_S0 (kSession) -> drops
  // challenge and cached HMAC sessions immediately and preserves kSession.
  EXPECT_CALL(proxy_->GetMockHmacSession(), StartUnboundSession(true, true))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  ASSERT_THAT(backend_->GetSessionManagementTpm2().GetOrCreateHmacSession(
                  SessionSecuritySetting::kSaltAndEncrypted),
              IsOk());
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result2 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result2);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate2;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate2));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate2, _))
      .WillOnce(Return(trunks::TPM_RC_REFERENCE_S0));

  StatusOr<brillo::SecureBlob> unseal_result2 =
      middleware.CallSync<&Backend::SignatureSealing::Unseal>(
          challenge_result2->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(unseal_result2);
  EXPECT_EQ(unseal_result2.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kSession);
  EXPECT_THAT(backend_->GetSessionManagementTpm2().FlushInvalidSessions(),
              NotOk());
  StatusOr<brillo::SecureBlob> cleared_check2 =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result2->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(cleared_check2);
  EXPECT_EQ(cleared_check2.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);

  // Case 3: UnsealData fails with TPM_RC_POLICY_FAIL (kNoRetry) -> drops the
  // consumed challenge policy session immediately.
  StatusOr<Backend::SignatureSealing::ChallengeResult> challenge_result3 =
      SetupChallenge(seal_result.value());
  ASSERT_OK(challenge_result3);

  EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_key_name_),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(
      proxy_->GetMockPolicySession(),
      PolicySigned(_, fake_key_name_, fake_tpm_nonce_, _, _, _, _, nullptr))
      .WillOnce(Return(trunks::TPM_RC_SUCCESS));
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(fake_digests3_),
                      Return(trunks::TPM_RC_SUCCESS)));
  trunks::MockAuthorizationDelegate delegate3;
  EXPECT_CALL(proxy_->GetMockPolicySession(), GetDelegate())
      .WillOnce(Return(&delegate3));
  EXPECT_CALL(proxy_->GetMockTpmUtility(),
              UnsealData(trunks_sealed_data_, &delegate3, _))
      .WillOnce(Return(trunks::TPM_RC_POLICY_FAIL));

  StatusOr<brillo::SecureBlob> unseal_result3 =
      middleware.CallSync<&Backend::SignatureSealing::Unseal>(
          challenge_result3->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(unseal_result3);
  EXPECT_EQ(unseal_result3.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);
  StatusOr<brillo::SecureBlob> cleared_check3 =
      backend_->GetSignatureSealingTpm2().Unseal(
          challenge_result3->challenge_id,
          brillo::BlobFromString(fake_challenge_response_));
  ASSERT_NOT_OK(cleared_check3);
  EXPECT_EQ(cleared_check3.err_status()->ToTPMRetryAction(),
            TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
