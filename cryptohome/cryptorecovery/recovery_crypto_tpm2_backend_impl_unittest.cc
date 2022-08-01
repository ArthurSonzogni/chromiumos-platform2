// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <openssl/ec.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/openssl_utility.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_for_test.h>

#include "cryptohome/cryptorecovery/recovery_crypto_tpm2_backend_impl.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/tpm2_impl.h"

using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ScopedBN_CTX;
using testing::_;
using testing::DoAll;
using testing::Exactly;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace cryptohome {
namespace cryptorecovery {

class RecoveryCryptoTpm2BackendTest : public testing::Test {
 public:
  RecoveryCryptoTpm2BackendTest() {
    trunks_factory_.set_tpm_utility(&mock_tpm_utility_);
    trunks_factory_.set_hmac_session(&mock_hmac_session_);
    trunks_factory_.set_policy_session(&mock_policy_session_);
    trunks_factory_.set_trial_session(&mock_trial_session_);
    auto hwsec = std::make_unique<hwsec::MockCryptohomeFrontend>();
    hwsec_ = hwsec.get();
    tpm_ = std::make_unique<Tpm2Impl>(std::move(hwsec), &trunks_factory_,
                                      &mock_tpm_manager_utility_);
    recovery_crypto_tpm2_backend_ = tpm_->GetRecoveryCryptoBackend();
  }
  ~RecoveryCryptoTpm2BackendTest() = default;

  void SetUp() override {
    context_ = CreateBigNumContext();
    ASSERT_TRUE(context_);
    ec_256_ = EllipticCurve::Create(EllipticCurve::CurveType::kPrime256,
                                    context_.get());
    ASSERT_TRUE(ec_256_);
    ec_521_ = EllipticCurve::Create(EllipticCurve::CurveType::kPrime521,
                                    context_.get());
    ASSERT_TRUE(ec_521_);
  }

 protected:
  std::unique_ptr<cryptohome::Tpm2Impl> tpm_;
  hwsec::MockCryptohomeFrontend* hwsec_;
  cryptorecovery::RecoveryCryptoTpmBackend* recovery_crypto_tpm2_backend_;
  NiceMock<trunks::MockAuthorizationDelegate> mock_authorization_delegate_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  NiceMock<trunks::MockHmacSession> mock_hmac_session_;
  NiceMock<trunks::MockPolicySession> mock_policy_session_;
  NiceMock<trunks::MockPolicySession> mock_trial_session_;
  NiceMock<tpm_manager::MockTpmManagerUtility> mock_tpm_manager_utility_;

  ScopedBN_CTX context_;
  std::optional<EllipticCurve> ec_256_;
  std::optional<EllipticCurve> ec_521_;

 private:
  trunks::TrunksFactoryForTest trunks_factory_;
};

TEST_F(RecoveryCryptoTpm2BackendTest, GenerateKeyAuthValue) {
  EXPECT_EQ(recovery_crypto_tpm2_backend_->GenerateKeyAuthValue().to_string(),
            "");
}

TEST_F(RecoveryCryptoTpm2BackendTest, EncryptEccPrivateKeySuccess) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_trial_session_, StartUnboundSession(false, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .Times(Exactly(2))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_trial_session_, PolicyOR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  std::string expected_encrypted_own_priv_key("encrypted_private_key");
  EXPECT_CALL(mock_tpm_utility_,
              ImportECCKeyWithPolicyDigest(_, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<7>(expected_encrypted_own_priv_key),
                      Return(TPM_RC_SUCCESS)));

  EncryptEccPrivateKeyRequest tpm_backend_request({.ec = ec_256_.value(),
                                                   .own_key_pair = own_key_pair,
                                                   .auth_value = std::nullopt,
                                                   .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_TRUE(recovery_crypto_tpm2_backend_->EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(),
            expected_encrypted_own_priv_key);
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       EncryptEccPrivateKeyWithInvalidOwnKeyPair) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_hmac_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, StartUnboundSession(false, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, PolicyOR(_)).Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, GetDigest(_)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_,
              ImportECCKeyWithPolicyDigest(_, _, _, _, _, _, _, _))
      .Times(Exactly(0));

  // the input key pair is not on the elliptic curve 521
  EncryptEccPrivateKeyRequest tpm_backend_request({.ec = ec_521_.value(),
                                                   .own_key_pair = own_key_pair,
                                                   .auth_value = std::nullopt,
                                                   .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_FALSE(recovery_crypto_tpm2_backend_->EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(), "");
}

TEST_F(RecoveryCryptoTpm2BackendTest, EncryptEccPrivateKeyWithSessionFailure) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_hmac_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, StartUnboundSession(false, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, PolicyOR(_)).Times(Exactly(0));
  EXPECT_CALL(mock_trial_session_, GetDigest(_)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_,
              ImportECCKeyWithPolicyDigest(_, _, _, _, _, _, _, _))
      .Times(Exactly(0));

  EncryptEccPrivateKeyRequest tpm_backend_request({.ec = ec_256_.value(),
                                                   .own_key_pair = own_key_pair,
                                                   .auth_value = std::nullopt,
                                                   .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_FALSE(recovery_crypto_tpm2_backend_->EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(), "");
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       EncryptEccPrivateKeyWithImportEccKeyFailure) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_trial_session_, StartUnboundSession(false, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .Times(Exactly(2))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_trial_session_, PolicyOR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_,
              ImportECCKeyWithPolicyDigest(_, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  EncryptEccPrivateKeyRequest tpm_backend_request({.ec = ec_256_.value(),
                                                   .own_key_pair = own_key_pair,
                                                   .auth_value = std::nullopt,
                                                   .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_FALSE(recovery_crypto_tpm2_backend_->EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(), "");
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       GenerateDiffieHellmanSharedSecretSuccess) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Generated shared point has to be on the curve to be converted to
  // TPMS_ECC_POINT, EC_KEY. Therefore, it cannot be dummy value.
  crypto::ScopedEC_KEY expected_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(expected_key_pair);
  const EC_POINT* expected_shared_secret_point =
      EC_KEY_get0_public_key(expected_key_pair.get());
  ASSERT_TRUE(expected_shared_secret_point);
  trunks::TPMS_ECC_POINT expected_shared_secret_ecc_point;
  ASSERT_TRUE(trunks::OpensslToTpmEccPoint(
      *ec_256_->GetGroup(), *expected_shared_secret_point,
      ec_256_->AffineCoordinateSizeInBytes(),
      &expected_shared_secret_ecc_point));

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .Times(Exactly(1))
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDelegate())
      .Times(Exactly(1))
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, AddPcrValuesToPolicySession(_, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, PolicyOR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(trunks::Make_TPM2B_ECC_POINT(
                          expected_shared_secret_ecc_point)),
                      Return(TPM_RC_SUCCESS)));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm2_backend_->GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_NE(nullptr, shared_secret_point);
  EXPECT_TRUE(ec_256_->AreEqual(*shared_secret_point,
                                *expected_shared_secret_point, context_.get()));
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       GenerateDiffieHellmanSharedSecretWithInvalidOthersPublicPoint) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_hmac_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _)).Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, AddPcrValuesToPolicySession(_, _, _))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, PolicyOR(_)).Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDigest(_)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(_, _, _, _)).Times(Exactly(0));

  // the input key pair is not on the elliptic curve 521
  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_521_.value(),
       .encrypted_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm2_backend_->GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       GenerateDiffieHellmanSharedSecretWithSessionFailure) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_hmac_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _)).Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, AddPcrValuesToPolicySession(_, _, _))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, PolicyOR(_)).Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDigest(_)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(_, _, _, _)).Times(Exactly(0));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm2_backend_->GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       GenerateDiffieHellmanSharedSecretWithLoadKeyFailure) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDelegate()).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, AddPcrValuesToPolicySession(_, _, _))
      .Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, PolicyOR(_)).Times(Exactly(0));
  EXPECT_CALL(mock_policy_session_, GetDigest(_)).Times(Exactly(0));
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(_, _, _, _)).Times(Exactly(0));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm2_backend_->GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm2BackendTest,
       GenerateDiffieHellmanSharedSecretWithECDHZGenFailure) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up the mock expectations.
  EXPECT_CALL(mock_hmac_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .Times(Exactly(1))
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDelegate())
      .Times(Exactly(1))
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(_, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, AddPcrValuesToPolicySession(_, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, PolicyOR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, ECDHZGen(_, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm2_backend_->GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

}  // namespace cryptorecovery
}  // namespace cryptohome
