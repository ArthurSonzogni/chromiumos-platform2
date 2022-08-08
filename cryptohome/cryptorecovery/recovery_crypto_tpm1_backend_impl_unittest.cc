// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <openssl/ec.h>

#include "cryptohome/cryptorecovery/recovery_crypto_tpm1_backend_impl.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/tpm_impl.h"

using hwsec_foundation::BigNumToSecureBlob;
using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ScopedBN_CTX;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::Exactly;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace cryptohome {
namespace cryptorecovery {

namespace {
// Size of the auth_value blob to be randomly generated.
//
// The choice of this constant is dictated by the desire to provide sufficient
// amount of entropy as the authorization secret for the TPM_Seal command (but
// with taking into account that this authorization value is hashed by SHA-1
// by Trousers anyway).
constexpr int kAuthValueSizeBytes = 32;
}  // namespace

class RecoveryCryptoTpm1BackendTest : public testing::Test {
 public:
  RecoveryCryptoTpm1BackendTest() {}
  ~RecoveryCryptoTpm1BackendTest() = default;

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
  NiceMock<MockTpm> tpm_;
  cryptorecovery::RecoveryCryptoTpm1BackendImpl recovery_crypto_tpm1_backend_{
      &tpm_};

  ScopedBN_CTX context_;
  std::optional<EllipticCurve> ec_256_;
  std::optional<EllipticCurve> ec_521_;
};

TEST_F(RecoveryCryptoTpm1BackendTest, GenerateKeyAuthValue) {
  EXPECT_EQ(recovery_crypto_tpm1_backend_.GenerateKeyAuthValue().size(),
            kAuthValueSizeBytes);
}

TEST_F(RecoveryCryptoTpm1BackendTest, EncryptEccPrivateKeySuccess) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');
  std::string obfuscated_username = "obfuscated username";

  // Set up mock expectations
  std::map<uint32_t, brillo::Blob> default_pcr_map = {
      {0, brillo::BlobFromString("default_pcr_map")}};
  EXPECT_CALL(tpm_, GetPcrMap(obfuscated_username, /*use_extended_pcr=*/false))
      .WillOnce(Return(default_pcr_map));
  const brillo::SecureBlob expected_encrypted_own_priv_key(256, 'b');
  EXPECT_CALL(tpm_,
              SealToPcrWithAuthorization(_, auth_value, default_pcr_map, _))
      .WillOnce(DoAll(SetArgPointee<3>(expected_encrypted_own_priv_key),
                      ReturnError<hwsec::TPMErrorBase>()));
  std::map<uint32_t, brillo::Blob> extended_pcr_map = {
      {0, brillo::BlobFromString("extended_pcr_map")}};
  EXPECT_CALL(tpm_, GetPcrMap(obfuscated_username, /*use_extended_pcr=*/true))
      .WillOnce(Return(extended_pcr_map));
  const brillo::SecureBlob expected_extended_pcr_bound_own_priv_key(256, 'c');
  EXPECT_CALL(tpm_,
              SealToPcrWithAuthorization(_, auth_value, extended_pcr_map, _))
      .WillOnce(
          DoAll(SetArgPointee<3>(expected_extended_pcr_bound_own_priv_key),
                ReturnError<hwsec::TPMErrorBase>()));

  EncryptEccPrivateKeyRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .own_key_pair = own_key_pair,
       .auth_value = auth_value,
       .obfuscated_username = obfuscated_username});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_TRUE(recovery_crypto_tpm1_backend_.EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(),
            expected_encrypted_own_priv_key.to_string());
  EXPECT_EQ(tpm_backend_response.extended_pcr_bound_own_priv_key.to_string(),
            expected_extended_pcr_bound_own_priv_key.to_string());
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       EncryptEccPrivateKeyWithInvalidOwnKeyPair) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY wrong_curve_key_pair =
      ec_521_->GenerateKey(context_.get());
  ASSERT_TRUE(wrong_curve_key_pair);
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');

  // Set up mock expectations
  EXPECT_CALL(tpm_, SealToPcrWithAuthorization(_, auth_value, _, _))
      .Times(Exactly(0));

  // the input key pair is not on the elliptic curve 256
  EncryptEccPrivateKeyRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .own_key_pair = wrong_curve_key_pair,
       .auth_value = auth_value,
       .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_FALSE(recovery_crypto_tpm1_backend_.EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(), "");
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       EncryptEccPrivateKeyWithInvalidAuthValue) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);

  const BIGNUM* own_priv_key_bn = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key_bn);
  brillo::SecureBlob expected_privated_key;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key_bn, ec_256_->ScalarSizeInBytes(),
                                 &expected_privated_key));

  // Set up mock expectations
  EXPECT_CALL(tpm_, SealToPcrWithAuthorization(_, _, _, _)).Times(Exactly(0));

  EncryptEccPrivateKeyRequest tpm_backend_request({.ec = ec_256_.value(),
                                                   .own_key_pair = own_key_pair,
                                                   .auth_value = std::nullopt,
                                                   .obfuscated_username = ""});
  EncryptEccPrivateKeyResponse tpm_backend_response;
  EXPECT_TRUE(recovery_crypto_tpm1_backend_.EncryptEccPrivateKey(
      tpm_backend_request, &tpm_backend_response));
  EXPECT_EQ(tpm_backend_response.encrypted_own_priv_key.to_string(),
            expected_privated_key.to_string());
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       GenerateDiffieHellmanSharedSecretSuccess) {
  // Set up inputs to the test.
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const BIGNUM* own_priv_key = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key);
  brillo::SecureBlob own_priv_point_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key,
                                 ec_256_->AffineCoordinateSizeInBytes(),
                                 &own_priv_point_blob));

  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Calculate expected shared point
  crypto::ScopedEC_POINT expected_shared_secret = ComputeEcdhSharedSecretPoint(
      ec_256_.value(), *others_pub_key, *own_priv_key);
  ASSERT_TRUE(expected_shared_secret);

  // Set up mock expectations
  EXPECT_CALL(*tpm_.get_mock_hwsec(), IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, auth_value, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(own_priv_point_blob),
                      ReturnError<hwsec::TPMErrorBase>()));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = own_priv_point_blob,
       .extended_pcr_bound_own_priv_key = brillo::SecureBlob(),
       .auth_value = auth_value,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm1_backend_.GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_NE(shared_secret_point, nullptr);
  EXPECT_TRUE(ec_256_->AreEqual(*shared_secret_point, *expected_shared_secret,
                                context_.get()));
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       GenerateDiffieHellmanSharedSecretWithInvalidOthersPublicPoint) {
  // Set up inputs to the test.
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const BIGNUM* own_priv_key = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key);
  brillo::SecureBlob own_priv_point_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key,
                                 ec_256_->AffineCoordinateSizeInBytes(),
                                 &own_priv_point_blob));
  // other's public point is from ec_521 and is not valid for ec_256
  crypto::ScopedEC_KEY others_key_pair = ec_521_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_521_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up mock expectations
  EXPECT_CALL(*tpm_.get_mock_hwsec(), IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, auth_value, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(own_priv_point_blob),
                      ReturnError<hwsec::TPMErrorBase>()));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = own_priv_point_blob,
       .extended_pcr_bound_own_priv_key = brillo::SecureBlob(),
       .auth_value = auth_value,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm1_backend_.GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       GenerateDiffieHellmanSharedSecretWithErrorNoRetry) {
  // Set up inputs to the test.
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const BIGNUM* own_priv_key = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key);
  brillo::SecureBlob own_priv_point_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key,
                                 ec_256_->AffineCoordinateSizeInBytes(),
                                 &own_priv_point_blob));

  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up mock expectations
  EXPECT_CALL(*tpm_.get_mock_hwsec(), IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, auth_value, _, _))
      .WillOnce(ReturnError<hwsec::TPMError>("fake",
                                             hwsec::TPMRetryAction::kNoRetry));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = own_priv_point_blob,
       .extended_pcr_bound_own_priv_key = brillo::SecureBlob(),
       .auth_value = auth_value,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm1_backend_.GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       GenerateDiffieHellmanSharedSecretWithCommunicationErrorRetry) {
  // Set up inputs to the test.
  const brillo::SecureBlob auth_value(kAuthValueSizeBytes, 'a');
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const BIGNUM* own_priv_key = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key);
  brillo::SecureBlob own_priv_point_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key,
                                 ec_256_->AffineCoordinateSizeInBytes(),
                                 &own_priv_point_blob));

  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Set up mock expectations
  EXPECT_CALL(*tpm_.get_mock_hwsec(), IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, auth_value, _, _))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kCommunication));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = own_priv_point_blob,
       .extended_pcr_bound_own_priv_key = brillo::SecureBlob(),
       .auth_value = auth_value,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm1_backend_.GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_EQ(nullptr, shared_secret_point);
}

TEST_F(RecoveryCryptoTpm1BackendTest,
       GenerateDiffieHellmanSharedSecretWithInvalidAuthValue) {
  // Set up inputs to the test.
  crypto::ScopedEC_KEY own_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(own_key_pair);
  const BIGNUM* own_priv_key = EC_KEY_get0_private_key(own_key_pair.get());
  ASSERT_TRUE(own_priv_key);
  brillo::SecureBlob own_priv_point_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*own_priv_key,
                                 ec_256_->AffineCoordinateSizeInBytes(),
                                 &own_priv_point_blob));

  crypto::ScopedEC_KEY others_key_pair = ec_256_->GenerateKey(context_.get());
  ASSERT_TRUE(others_key_pair);
  const EC_POINT* others_pub_key_ptr =
      EC_KEY_get0_public_key(others_key_pair.get());
  ASSERT_TRUE(others_pub_key_ptr);
  crypto::ScopedEC_POINT others_pub_key(
      EC_POINT_dup(others_pub_key_ptr, ec_256_->GetGroup()));
  ASSERT_TRUE(others_pub_key);

  // Calculate expected shared point
  crypto::ScopedEC_POINT expected_shared_secret = ComputeEcdhSharedSecretPoint(
      ec_256_.value(), *others_pub_key, *own_priv_key);
  ASSERT_TRUE(expected_shared_secret);

  // Set up mock expectations
  EXPECT_CALL(*tpm_.get_mock_hwsec(), IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, _, _, _)).Times(Exactly(0));

  GenerateDhSharedSecretRequest tpm_backend_request(
      {.ec = ec_256_.value(),
       .encrypted_own_priv_key = own_priv_point_blob,
       .extended_pcr_bound_own_priv_key = brillo::SecureBlob(),
       .auth_value = std::nullopt,
       .obfuscated_username = "",
       .others_pub_point = std::move(others_pub_key)});
  crypto::ScopedEC_POINT shared_secret_point =
      recovery_crypto_tpm1_backend_.GenerateDiffieHellmanSharedSecret(
          tpm_backend_request);
  EXPECT_NE(nullptr, shared_secret_point);
  EXPECT_TRUE(ec_256_->AreEqual(*shared_secret_point, *expected_shared_secret,
                                context_.get()));
}

}  // namespace cryptorecovery
}  // namespace cryptohome
