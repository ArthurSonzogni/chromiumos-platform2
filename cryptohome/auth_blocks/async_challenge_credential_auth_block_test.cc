// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/async_challenge_credential_auth_block.h"

#include <stdint.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"
#include "cryptohome/challenge_credentials/challenge_credentials_test_utils.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/signature_sealing_test_utils.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/proto_bindings/key.pb.h"
#include "cryptohome/proto_bindings/rpc.pb.h"

using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

using HwsecAlgorithm = hwsec::CryptohomeFrontend::SignatureSealingAlgorithm;

using testing::_;
using testing::AtLeast;
using testing::NiceMock;
using testing::Return;

namespace cryptohome {

namespace {

MATCHER_P(ChallengeAlgorithmIs, algorithm, "") {
  return arg.signature_request_data().signature_algorithm() == algorithm;
}

void VerifyCreateCallback(base::RunLoop* run_loop,
                          AuthInput* auth_input,
                          CryptoStatus error,
                          std::unique_ptr<KeyBlobs> blobs,
                          std::unique_ptr<AuthBlockState> auth_state) {
  ASSERT_TRUE(error.ok());

  // Because the salt is generated randomly inside the auth block, this
  // test cannot check the exact values returned. The salt() could be
  // passed through in some test specific harness, but the underlying
  // scrypt code is tested in so many other places, it's unnecessary.
  EXPECT_FALSE(blobs->scrypt_key->derived_key().empty());
  EXPECT_FALSE(blobs->scrypt_key->ConsumeSalt().empty());

  EXPECT_FALSE(blobs->chaps_scrypt_key->derived_key().empty());
  EXPECT_FALSE(blobs->chaps_scrypt_key->ConsumeSalt().empty());

  EXPECT_FALSE(blobs->scrypt_wrapped_reset_seed_key->derived_key().empty());
  EXPECT_FALSE(blobs->scrypt_wrapped_reset_seed_key->ConsumeSalt().empty());

  ASSERT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
      auth_state->state));

  auto& tpm_state =
      std::get<ChallengeCredentialAuthBlockState>(auth_state->state);

  ASSERT_TRUE(tpm_state.keyset_challenge_info.has_value());
  EXPECT_EQ(
      tpm_state.keyset_challenge_info.value().public_key_spki_der,
      auth_input->challenge_credential_auth_input.value().public_key_spki_der);
  EXPECT_EQ(tpm_state.keyset_challenge_info.value().salt_signature_algorithm,
            auth_input->challenge_credential_auth_input.value()
                .challenge_signature_algorithms[0]);
  run_loop->Quit();
}
}  // namespace

class AsyncChallengeCredentialAuthBlockTest : public ::testing::Test {
 public:
  AsyncChallengeCredentialAuthBlockTest() = default;
  AsyncChallengeCredentialAuthBlockTest(
      const AsyncChallengeCredentialAuthBlockTest&) = delete;
  AsyncChallengeCredentialAuthBlockTest& operator=(
      const AsyncChallengeCredentialAuthBlockTest&) = delete;

  ~AsyncChallengeCredentialAuthBlockTest() override = default;

  void SetUp() override {
    auto mock_key_challenge_service =
        std::make_unique<NiceMock<MockKeyChallengeService>>();
    auth_block_ = std::make_unique<AsyncChallengeCredentialAuthBlock>(
        &challenge_credentials_helper_, std::move(mock_key_challenge_service),
        kFakeAccountId);
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  const std::string kFakeAccountId = "account_id";
  std::unique_ptr<AsyncChallengeCredentialAuthBlock> auth_block_;

  const error::CryptohomeError::ErrorLocationPair kErrorLocationPlaceholder =
      error::CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          "Testing1");
};

// The AsyncChallengeCredentialAuthBlock::Create should work correctly.
TEST_F(AsyncChallengeCredentialAuthBlockTest, Create) {
  AuthInput auth_input{
      .obfuscated_username = "obfuscated_username",
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };

  EXPECT_CALL(challenge_credentials_helper_,
              GenerateNew(kFakeAccountId, _, _, _, _))
      .WillOnce([&](auto&&, auto public_key_info, auto&&, auto&&,
                    auto&& callback) {
        auto info = std::make_unique<structure::SignatureChallengeInfo>();
        info->public_key_spki_der = public_key_info.public_key_spki_der;
        info->salt_signature_algorithm = public_key_info.signature_algorithm[0];
        auto passkey = std::make_unique<brillo::SecureBlob>("passkey");
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                std::move(info), std::move(passkey)));
      });

  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback =
      base::BindOnce(VerifyCreateCallback, &run_loop, &auth_input);

  auth_block_->Create(auth_input, std::move(create_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Create should fail when the challenge
// service failed.
TEST_F(AsyncChallengeCredentialAuthBlockTest, CreateCredentialsFailed) {
  EXPECT_CALL(challenge_credentials_helper_,
              GenerateNew(kFakeAccountId, _, _, _, _))
      .WillOnce(
          [&](auto&&, auto public_key_info, auto&&, auto&&, auto&& callback) {
            std::move(callback).Run(MakeStatus<CryptohomeTPMError>(
                kErrorLocationPlaceholder,
                ErrorActionSet({ErrorAction::kIncorrectAuth}),
                TPMRetryAction::kUserAuth));
          });

  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_TPM_CRYPTO);
        run_loop.Quit();
      });

  AuthInput auth_input{
      .obfuscated_username = "obfuscated_username",
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };

  auth_block_->Create(auth_input, std::move(create_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Create should fail when called
// multiple create.
TEST_F(AsyncChallengeCredentialAuthBlockTest, MutipleCreateFailed) {
  AuthInput auth_input{
      .obfuscated_username = "obfuscated_username",
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };

  EXPECT_CALL(challenge_credentials_helper_,
              GenerateNew(kFakeAccountId, _, _, _, _))
      .WillOnce([&](auto&&, auto public_key_info, auto&&, auto&&,
                    auto&& callback) {
        auto info = std::make_unique<structure::SignatureChallengeInfo>();
        info->public_key_spki_der = public_key_info.public_key_spki_der;
        info->salt_signature_algorithm = public_key_info.signature_algorithm[0];
        auto passkey = std::make_unique<brillo::SecureBlob>("passkey");
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                std::move(info), std::move(passkey)));
      });

  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback =
      base::BindOnce(VerifyCreateCallback, &run_loop, &auth_input);

  auth_block_->Create(auth_input, std::move(create_callback));

  run_loop.Run();

  base::RunLoop run_loop2;
  AuthBlock::CreateCallback create_callback2 = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // The second create would failed.
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop2.Quit();
      });

  auth_block_->Create(auth_input, std::move(create_callback2));

  run_loop2.Run();
}

// The AsyncChallengeCredentialAuthBlock::Create should fail when missing
// obfuscated username.
TEST_F(AsyncChallengeCredentialAuthBlockTest, CreateMissingObfuscatedUsername) {
  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  AuthInput auth_input{
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };
  auth_block_->Create(auth_input, std::move(create_callback));
  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Create should fail when missing auth
// input.
TEST_F(AsyncChallengeCredentialAuthBlockTest,
       CreateMissingChallengeCredentialAuthInput) {
  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  AuthInput auth_input{
      .obfuscated_username = "obfuscated_username",
  };
  auth_block_->Create(auth_input, std::move(create_callback));
  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Create should fail when missing
// algorithm.
TEST_F(AsyncChallengeCredentialAuthBlockTest, CreateMissingAlgorithm) {
  base::RunLoop run_loop;
  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  AuthInput auth_input{
      .obfuscated_username = "obfuscated_username",
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
          },
  };
  auth_block_->Create(auth_input, std::move(create_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should work correctly.
TEST_F(AsyncChallengeCredentialAuthBlockTest, Derive) {
  // These blobs were introduced in https://crrev.com/c/2292973.
  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .scrypt_state =
                  LibScryptCompatAuthBlockState{
                      .wrapped_keyset =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0x4D, 0xEE, 0xFC, 0x79, 0x0D, 0x79, 0x08, 0x79,
                              0xD5, 0xF6, 0x07, 0x65, 0xDF, 0x76, 0x5A, 0xAE,
                              0xD1, 0xBD, 0x1D, 0xCF, 0x29, 0xF6, 0xFF, 0x5C,
                              0x31, 0x30, 0x23, 0xD1, 0x22, 0x17, 0xDF, 0x74,
                              0x26, 0xD5, 0x11, 0x88, 0x8D, 0x40, 0xA6, 0x9C,
                              0xB9, 0x72, 0xCE, 0x37, 0x71, 0xB7, 0x39, 0x0E,
                              0x3E, 0x34, 0x0F, 0x73, 0x29, 0xF4, 0x0F, 0x89,
                              0x15, 0xF7, 0x6E, 0xA1, 0x5A, 0x29, 0x78, 0x21,
                              0xB7, 0xC0, 0x76, 0x50, 0x14, 0x5C, 0xAD, 0x77,
                              0x53, 0xC9, 0xD0, 0xFE, 0xD1, 0xB9, 0x81, 0x32,
                              0x75, 0x0E, 0x1E, 0x45, 0x34, 0xBD, 0x0B, 0xF7,
                              0xFA, 0xED, 0x9A, 0xD7, 0x6B, 0xE4, 0x2F, 0xC0,
                              0x2F, 0x58, 0xBE, 0x3A, 0x26, 0xD1, 0x82, 0x41,
                              0x09, 0x82, 0x7F, 0x17, 0xA8, 0x5C, 0x66, 0x0E,
                              0x24, 0x8B, 0x7B, 0xF5, 0xEB, 0x0C, 0x6D, 0xAE,
                              0x19, 0x5C, 0x7D, 0xC4, 0x0D, 0x8D, 0xB2, 0x18,
                              0x13, 0xD4, 0xC0, 0x32, 0x34, 0x15, 0xAE, 0x1D,
                              0xA1, 0x44, 0x2E, 0x80, 0xD8, 0x00, 0x8A, 0xB9,
                              0xDD, 0xA4, 0xC0, 0x33, 0xAE, 0x26, 0xD3, 0xE6,
                              0x53, 0xD6, 0x31, 0x5C, 0x4C, 0x10, 0xBB, 0xA9,
                              0xD5, 0x53, 0xD7, 0xAD, 0xCD, 0x97, 0x20, 0x83,
                              0xFC, 0x18, 0x4B, 0x7F, 0xC1, 0xBD, 0x85, 0x43,
                              0x12, 0x85, 0x4F, 0x6F, 0xAA, 0xDB, 0x58, 0xA0,
                              0x0F, 0x2C, 0xAB, 0xEA, 0x74, 0x8E, 0x2C, 0x28,
                              0x01, 0x88, 0x48, 0xA5, 0x0A, 0xFC, 0x2F, 0xB4,
                              0x59, 0x4B, 0xF6, 0xD9, 0xE5, 0x47, 0x94, 0x42,
                              0xA5, 0x61, 0x06, 0x8C, 0x5A, 0x9C, 0xD3, 0xA6,
                              0x30, 0x2C, 0x13, 0xCA, 0xF1, 0xFF, 0xFE, 0x5C,
                              0xE8, 0x21, 0x25, 0x9A, 0xE0, 0x50, 0xC3, 0x2F,
                              0x14, 0x71, 0x38, 0xD0, 0xE7, 0x79, 0x5D, 0xF0,
                              0x71, 0x80, 0xF0, 0x3D, 0x05, 0xB6, 0xF7, 0x67,
                              0x3F, 0x22, 0x21, 0x7A, 0xED, 0x48, 0xC4, 0x2D,
                              0xEA, 0x2E, 0xAE, 0xE9, 0xA8, 0xFF, 0xA0, 0xB6,
                              0xB4, 0x0A, 0x94, 0x34, 0x40, 0xD1, 0x6C, 0x6C,
                              0xC7, 0x90, 0x9C, 0xF7, 0xED, 0x0B, 0xED, 0x90,
                              0xB1, 0x4D, 0x6D, 0xB4, 0x3D, 0x04, 0x7E, 0x7B,
                              0x16, 0x59, 0xFF, 0xFE},
                      .wrapped_chaps_key =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0xC9, 0x80, 0xA1, 0x30, 0x82, 0x40, 0xE6, 0xCF,
                              0xC8, 0x59, 0xE9, 0xB6, 0xB0, 0xE8, 0xBF, 0x95,
                              0x82, 0x79, 0x71, 0xF9, 0x86, 0x8A, 0xCA, 0x53,
                              0x23, 0xCF, 0x31, 0xFE, 0x4B, 0xD2, 0xA5, 0x26,
                              0xA4, 0x46, 0x3D, 0x35, 0xEF, 0x69, 0x02, 0xC4,
                              0xBF, 0x72, 0xDC, 0xF8, 0x90, 0x77, 0xFB, 0x59,
                              0x0D, 0x41, 0xCB, 0x5B, 0x58, 0xC6, 0x08, 0x0F,
                              0x19, 0x4E, 0xC8, 0x4A, 0x57, 0xE7, 0x63, 0x43,
                              0x39, 0x79, 0xD7, 0x6E, 0x0D, 0xD0, 0xE4, 0x4F,
                              0xFA, 0x55, 0x32, 0xE1, 0x6B, 0xE4, 0xFF, 0x12,
                              0xB1, 0xA3, 0x75, 0x9C, 0x44, 0x3A, 0x16, 0x68,
                              0x5C, 0x11, 0xD0, 0xA5, 0x4C, 0x65, 0xB0, 0xBF,
                              0x04, 0x41, 0x94, 0xFE, 0xC5, 0xDD, 0x5C, 0x78,
                              0x5B, 0x14, 0xA1, 0x3F, 0x0B, 0x17, 0x9C, 0x75,
                              0xA5, 0x9E, 0x36, 0x14, 0x5B, 0xC4, 0xAC, 0x77,
                              0x28, 0xDE, 0xEB, 0xB4, 0x51, 0x5F, 0x33, 0x36},
                      .wrapped_reset_seed =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0x7F, 0x40, 0x30, 0x51, 0x2F, 0x15, 0x62, 0x15,
                              0xB1, 0x2E, 0x58, 0x27, 0x52, 0xE4, 0xFF, 0xC5,
                              0x3C, 0x1E, 0x19, 0x05, 0x84, 0xD8, 0xE8, 0xD4,
                              0xFD, 0x8C, 0x33, 0xE8, 0x06, 0x1A, 0x38, 0x28,
                              0x2D, 0xD7, 0x01, 0xD2, 0xB3, 0xE1, 0x95, 0xC3,
                              0x49, 0x63, 0x39, 0xA2, 0xB2, 0xE3, 0xDA, 0xE2,
                              0x76, 0x40, 0x40, 0x11, 0xD1, 0x98, 0xD2, 0x03,
                              0xFB, 0x60, 0xD0, 0xA1, 0xA5, 0xB5, 0x51, 0xAA,
                              0xEF, 0x6C, 0xB3, 0xAB, 0x23, 0x65, 0xCA, 0x44,
                              0x84, 0x7A, 0x71, 0xCA, 0x0C, 0x36, 0x33, 0x7F,
                              0x53, 0x06, 0x0E, 0x03, 0xBB, 0xC1, 0x9A, 0x9D,
                              0x40, 0x1C, 0x2F, 0x46, 0xB7, 0x84, 0x00, 0x59,
                              0x5B, 0xD6, 0x53, 0xE4, 0x51, 0x82, 0xC2, 0x3D,
                              0xF4, 0x46, 0xD2, 0xDD, 0xE5, 0x7A, 0x0A, 0xEB,
                              0xC8, 0x45, 0x7C, 0x37, 0x01, 0xD5, 0x37, 0x4E,
                              0xE3, 0xC7, 0xBC, 0xC6, 0x5E, 0x25, 0xFE, 0xE2,
                              0x05, 0x14, 0x60, 0x33, 0xB8, 0x1A, 0xF1, 0x17,
                              0xE1, 0x0C, 0x25, 0x00, 0xA5, 0x0A, 0xD5, 0x03},
                  },
              .keyset_challenge_info =
                  structure::SignatureChallengeInfo{
                      .public_key_spki_der =
                          brillo::BlobFromString("public_key_spki_der"),
                      .salt_signature_algorithm = structure::
                          ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
                  },
          },
  };

  brillo::SecureBlob scrypt_passkey = {
      0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36, 0x35, 0x31, 0x30,
      0x65, 0x30, 0x64, 0x35, 0x64, 0x35, 0x35, 0x36, 0x35, 0x35, 0x35,
      0x38, 0x36, 0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  brillo::SecureBlob derived_key = {
      0x58, 0x2A, 0x41, 0x1F, 0xC0, 0x27, 0x2D, 0xC7, 0xF8, 0xEC, 0xA3,
      0x4E, 0xC0, 0x3F, 0x6C, 0x56, 0x6D, 0x88, 0x69, 0x3F, 0x50, 0x20,
      0x37, 0xE3, 0x77, 0x5F, 0xDD, 0xC3, 0x61, 0x2D, 0x27, 0xAD, 0xD3,
      0x55, 0x4D, 0x66, 0xE5, 0x83, 0xD2, 0x5E, 0x02, 0x0C, 0x22, 0x59,
      0x6C, 0x39, 0x35, 0x86, 0xEC, 0x46, 0xB0, 0x85, 0x89, 0xE3, 0x4C,
      0xB9, 0xE2, 0x0C, 0xA1, 0x27, 0x60, 0x85, 0x5A, 0x37};

  brillo::SecureBlob derived_chaps_key = {
      0x16, 0x53, 0xEE, 0x4D, 0x76, 0x47, 0x68, 0x09, 0xB3, 0x39, 0x1D,
      0xD3, 0x6F, 0xA2, 0x8F, 0x8A, 0x3E, 0xB3, 0x64, 0xDD, 0x4D, 0xC4,
      0x64, 0x6F, 0xE1, 0xB8, 0x82, 0x28, 0x68, 0x72, 0x68, 0x84, 0x93,
      0xE2, 0xDB, 0x2F, 0x27, 0x91, 0x08, 0x2C, 0xA0, 0xD9, 0xA1, 0x6E,
      0x6F, 0x0E, 0x13, 0x66, 0x1D, 0x94, 0x12, 0x6F, 0xF4, 0x98, 0x7B,
      0x44, 0x62, 0x57, 0x47, 0x33, 0x46, 0xD2, 0x30, 0x42};

  brillo::SecureBlob derived_reset_seed_key = {
      0xFA, 0x93, 0x57, 0xCE, 0x21, 0xBB, 0x82, 0x4D, 0x3A, 0x3B, 0x26,
      0x88, 0x8C, 0x7E, 0x61, 0x52, 0x52, 0xF0, 0x12, 0x25, 0xA3, 0x59,
      0xCA, 0x71, 0xD2, 0x0C, 0x52, 0x8A, 0x5B, 0x7A, 0x7D, 0xBF, 0x8E,
      0xC7, 0x4D, 0x1D, 0xB5, 0xF9, 0x01, 0xA6, 0xE5, 0x5D, 0x47, 0x2E,
      0xFD, 0x7C, 0x78, 0x1D, 0x9B, 0xAD, 0xE6, 0x71, 0x35, 0x2B, 0x32,
      0x1E, 0x59, 0x19, 0x47, 0x88, 0x92, 0x50, 0x28, 0x09};

  AuthInput auth_input{
      .locked_to_single_user = true,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };

  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kFakeAccountId, _, _, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&& callback) {
        auto passkey = std::make_unique<brillo::SecureBlob>(scrypt_passkey);
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                nullptr, std::move(passkey)));
      });

  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        ASSERT_TRUE(error.ok());
        EXPECT_EQ(derived_key, blobs->scrypt_key->derived_key());
        EXPECT_EQ(derived_chaps_key, blobs->chaps_scrypt_key->derived_key());
        EXPECT_EQ(derived_reset_seed_key,
                  blobs->scrypt_wrapped_reset_seed_key->derived_key());
        run_loop.Quit();
      });

  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should fail when the key
// challenge service failed.
TEST_F(AsyncChallengeCredentialAuthBlockTest, DeriveFailed) {
  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .keyset_challenge_info =
                  structure::SignatureChallengeInfo{
                      .public_key_spki_der =
                          brillo::BlobFromString("public_key_spki_der"),
                      .salt_signature_algorithm = structure::
                          ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
                  },
          },
  };

  AuthInput auth_input{
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };

  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kFakeAccountId, _, _, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&& callback) {
        std::move(callback).Run(MakeStatus<CryptohomeTPMError>(
            kErrorLocationPlaceholder,
            ErrorActionSet({ErrorAction::kIncorrectAuth}),
            TPMRetryAction::kUserAuth));
      });

  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_TPM_CRYPTO);
        run_loop.Quit();
      });

  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should fail when missing
// algorithms.
TEST_F(AsyncChallengeCredentialAuthBlockTest, DeriveMissingAlgorithms) {
  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  AuthBlockState auth_state{};
  AuthInput auth_input{
      .locked_to_single_user = false,
  };
  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));
  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should fail when missing state.
TEST_F(AsyncChallengeCredentialAuthBlockTest, DeriveNoState) {
  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_FATAL);
        run_loop.Quit();
      });

  AuthBlockState auth_state{};
  AuthInput auth_input{
      .locked_to_single_user = false,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };
  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));
  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should fail when missing keyset
// info.
TEST_F(AsyncChallengeCredentialAuthBlockTest, DeriveNoKeysetInfo) {
  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  AuthBlockState auth_state{
      .state = ChallengeCredentialAuthBlockState{},
  };
  AuthInput auth_input{
      .locked_to_single_user = false,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };
  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));

  run_loop.Run();
}

// The AsyncChallengeCredentialAuthBlock::Derive should fail when missing scrypt
// state.
TEST_F(AsyncChallengeCredentialAuthBlockTest, DeriveNoScryptState) {
  base::RunLoop run_loop;
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        run_loop.Quit();
      });

  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kFakeAccountId, _, _, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&& callback) {
        auto passkey = std::make_unique<brillo::SecureBlob>("passkey");
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                nullptr, std::move(passkey)));
      });

  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .keyset_challenge_info =
                  structure::SignatureChallengeInfo{
                      .public_key_spki_der =
                          brillo::BlobFromString("public_key_spki_der"),
                      .salt_signature_algorithm = structure::
                          ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
                  },
          },
  };
  AuthInput auth_input{
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
          },
  };
  auth_block_->Derive(auth_input, auth_state, std::move(derive_callback));

  run_loop.Run();
}

// Test fixture that sets up a real `ChallengeCredentialsHelperImpl` and mocks
// at the `SignatureSealingBackend` level, hence achieving more extensive test
// coverage than the fixture above.
class AsyncChallengeCredentialAuthBlockFullTest : public ::testing::Test {
 protected:
  const std::string kObfuscatedUsername = "obfuscated_username";
  const brillo::Blob kPublicKeySpkiDer =
      brillo::BlobFromString("public_key_spki_der");

  AsyncChallengeCredentialAuthBlockFullTest() {
    EXPECT_CALL(hwsec_, GetRandomBlob(_)).WillRepeatedly([](size_t length) {
      return brillo::Blob(length, 0);
    });

    challenge_credentials_helper_ =
        std::make_unique<ChallengeCredentialsHelperImpl>(&hwsec_);
  }

  ~AsyncChallengeCredentialAuthBlockFullTest() = default;

  void CreateAuthBlock() {
    auto owned_key_challenge_service =
        std::make_unique<MockKeyChallengeService>();
    key_challenge_service_ = owned_key_challenge_service.get();
    auth_block_ = std::make_unique<AsyncChallengeCredentialAuthBlock>(
        challenge_credentials_helper_.get(),
        std::move(owned_key_challenge_service), kFakeAccountId);
  }

  void BackendWillSeal(const std::vector<HwsecAlgorithm>& key_algorithms) {
    EXPECT_CALL(hwsec_, GetRandomSecureBlob(_))
        .WillOnce(ReturnValue(kTpmProtectedSecret));

    SignatureSealedCreationMocker mocker(&hwsec_);
    mocker.set_public_key_spki_der(kPublicKeySpkiDer);
    mocker.set_key_algorithms(key_algorithms);
    mocker.set_obfuscated_username(kObfuscatedUsername);
    mocker.set_secret_value(kTpmProtectedSecret);
    mocker.SetUpSuccessfulMock();
  }

  void BackendWillUnseal(const std::vector<HwsecAlgorithm>& key_algorithms,
                         HwsecAlgorithm unsealing_algorithm) {
    SignatureSealedUnsealingMocker mocker(&hwsec_);
    mocker.set_public_key_spki_der(kPublicKeySpkiDer);
    mocker.set_key_algorithms(key_algorithms);
    mocker.set_chosen_algorithm(unsealing_algorithm);
    mocker.set_challenge_value(brillo::BlobFromString("challenge"));
    mocker.set_challenge_signature(kChallengeResponse);
    mocker.set_secret_value(kTpmProtectedSecret);
    mocker.SetUpSuccessfulMock();
  }

  void ChallengesWillRespond(ChallengeSignatureAlgorithm algorithm) {
    DCHECK(key_challenge_service_);
    EXPECT_CALL(*key_challenge_service_,
                ChallengeKeyMovable(_, ChallengeAlgorithmIs(algorithm), _))
        .Times(AtLeast(1))
        .WillRepeatedly([&](const AccountIdentifier& account_id,
                            const KeyChallengeRequest& request,
                            KeyChallengeService::ResponseCallback* callback) {
          auto response = std::make_unique<KeyChallengeResponse>();
          response->mutable_signature_response_data()->set_signature(
              brillo::BlobToString(kChallengeResponse));
          std::move(*callback).Run(std::move(response));
        });
  }

  CryptoStatus RunCreate(
      const AuthInput& auth_input,
      std::unique_ptr<KeyBlobs>& out_key_blobs,
      std::unique_ptr<AuthBlockState>& out_auth_block_state) {
    DCHECK(auth_block_);
    base::RunLoop run_loop;
    CryptoStatus got_error;
    auth_block_->Create(
        auth_input,
        base::BindLambdaForTesting(
            [&](CryptoStatus error, std::unique_ptr<KeyBlobs> key_blobs,
                std::unique_ptr<AuthBlockState> auth_block_state) {
              got_error = std::move(error);
              out_key_blobs = std::move(key_blobs);
              out_auth_block_state = std::move(auth_block_state);
              run_loop.Quit();
            }));
    run_loop.Run();
    return got_error;
  }

  CryptoStatus RunDerive(const AuthInput& auth_input,
                         const AuthBlockState& auth_block_state,
                         std::unique_ptr<KeyBlobs>& out_key_blobs) {
    DCHECK(auth_block_);
    base::RunLoop run_loop;
    CryptoStatus got_error;
    auth_block_->Derive(
        auth_input, auth_block_state,
        base::BindLambdaForTesting(
            [&](CryptoStatus error, std::unique_ptr<KeyBlobs> key_blobs) {
              got_error = std::move(error);
              out_key_blobs = std::move(key_blobs);
              run_loop.Quit();
            }));
    run_loop.Run();
    return got_error;
  }

  void FillScryptWrappedKeyset(const KeyBlobs& key_blobs,
                               AuthBlockState& auth_block_state) {
    ASSERT_TRUE(key_blobs.scrypt_key);
    const auto blob_to_encrypt = brillo::SecureBlob(brillo::CombineBlobs(
        {kScryptPlaintext, hwsec_foundation::Sha1(kScryptPlaintext)}));
    brillo::SecureBlob ciphertext;
    ASSERT_TRUE(hwsec_foundation::LibScryptCompat::Encrypt(
        key_blobs.scrypt_key->derived_key(),
        key_blobs.scrypt_key->ConsumeSalt(), blob_to_encrypt,
        hwsec_foundation::kDefaultScryptParams, &ciphertext));
    ChallengeCredentialAuthBlockState* cc_state =
        std::get_if<ChallengeCredentialAuthBlockState>(&auth_block_state.state);
    ASSERT_TRUE(cc_state);
    cc_state->scrypt_state.wrapped_keyset = ciphertext;
  }

 private:
  const std::string kFakeAccountId = "account_id";
  const brillo::SecureBlob kTpmProtectedSecret =
      brillo::SecureBlob("tpm_protected_secret");
  const brillo::Blob kChallengeResponse = brillo::BlobFromString("signature");
  const brillo::Blob kScryptPlaintext = brillo::BlobFromString("plaintext");

  base::test::TaskEnvironment task_environment_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  std::unique_ptr<ChallengeCredentialsHelperImpl> challenge_credentials_helper_;
  std::unique_ptr<AsyncChallengeCredentialAuthBlock> auth_block_;
  // Unowned - pointing to the object owned by `*auth_block_`.
  MockKeyChallengeService* key_challenge_service_ = nullptr;
};

// Verifies that Derive succeeds on the output of Create.
TEST_F(AsyncChallengeCredentialAuthBlockFullTest, DeriveCreated) {
  constexpr auto kHwsecAlgorithm = HwsecAlgorithm::kRsassaPkcs1V15Sha256;
  constexpr auto kAlgorithm =
      structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256;
  constexpr auto kChallengeAlgorithm = CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
  const AuthInput kAuthInput{
      .obfuscated_username = kObfuscatedUsername,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der = kPublicKeySpkiDer,
              .challenge_signature_algorithms = {kAlgorithm},
          },
  };

  // Setup: create an auth block state.
  CreateAuthBlock();
  BackendWillSeal({kHwsecAlgorithm});
  ChallengesWillRespond(kChallengeAlgorithm);
  std::unique_ptr<KeyBlobs> created_key_blobs;
  std::unique_ptr<AuthBlockState> auth_block_state;
  ASSERT_THAT(RunCreate(kAuthInput, created_key_blobs, auth_block_state),
              IsOk());
  ASSERT_TRUE(created_key_blobs);
  ASSERT_TRUE(auth_block_state);
  // Backfill the scrypt wrapped_keyset, to mimic how the caller uses
  // scrypt-based auth blocks for derivation.
  ASSERT_NO_FATAL_FAILURE(
      FillScryptWrappedKeyset(*created_key_blobs, *auth_block_state));

  // Test: run the derivation.
  CreateAuthBlock();
  BackendWillUnseal({kHwsecAlgorithm}, kHwsecAlgorithm);
  ChallengesWillRespond(kChallengeAlgorithm);
  std::unique_ptr<KeyBlobs> derived_key_blobs;
  ASSERT_THAT(RunDerive(kAuthInput, *auth_block_state, derived_key_blobs),
              IsOk());
  ASSERT_TRUE(derived_key_blobs);

  // Assert: verify the derivation gives the same secret as the creation.
  ASSERT_TRUE(created_key_blobs->scrypt_key);
  ASSERT_TRUE(derived_key_blobs->scrypt_key);
  EXPECT_EQ(derived_key_blobs->scrypt_key->derived_key(),
            created_key_blobs->scrypt_key->derived_key());
}

// Verifies that Derive succeeds on the output of Create, even when different
// algorithms are used for salt and for the TPM-backed secret.
TEST_F(AsyncChallengeCredentialAuthBlockFullTest,
       DeriveCreatedDifferentAlgorithms) {
  constexpr auto kHwsecSaltAlgorithm = HwsecAlgorithm::kRsassaPkcs1V15Sha256;
  constexpr auto kSaltAlgorithm =
      structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256;
  constexpr auto kSaltChallengeAlgorithm = CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
  constexpr auto kHwsecTpmAlgorithm = HwsecAlgorithm::kRsassaPkcs1V15Sha1;
  constexpr auto kTpmAlgorithm =
      structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1;
  constexpr auto kTpmChallengeAlgorithm = CHALLENGE_RSASSA_PKCS1_V1_5_SHA1;
  const std::vector<HwsecAlgorithm> kHwsecAlgorithms = {kHwsecTpmAlgorithm,
                                                        kHwsecSaltAlgorithm};
  const std::vector<structure::ChallengeSignatureAlgorithm> kAlgorithms = {
      kTpmAlgorithm, kSaltAlgorithm};
  const AuthInput kAuthInput{
      .obfuscated_username = kObfuscatedUsername,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der = kPublicKeySpkiDer,
              .challenge_signature_algorithms = kAlgorithms,
          },
  };

  // Setup: create an auth block state.
  CreateAuthBlock();
  BackendWillSeal(kHwsecAlgorithms);
  ChallengesWillRespond(kSaltChallengeAlgorithm);
  std::unique_ptr<KeyBlobs> created_key_blobs;
  std::unique_ptr<AuthBlockState> auth_block_state;
  ASSERT_THAT(RunCreate(kAuthInput, created_key_blobs, auth_block_state),
              IsOk());
  ASSERT_TRUE(created_key_blobs);
  ASSERT_TRUE(auth_block_state);
  // Backfill the scrypt wrapped_keyset, to mimic how the caller uses
  // scrypt-based auth blocks for derivation.
  ASSERT_NO_FATAL_FAILURE(
      FillScryptWrappedKeyset(*created_key_blobs, *auth_block_state));

  // Test: run the derivation.
  CreateAuthBlock();
  BackendWillUnseal(kHwsecAlgorithms, kHwsecTpmAlgorithm);
  ChallengesWillRespond(kSaltChallengeAlgorithm);
  ChallengesWillRespond(kTpmChallengeAlgorithm);
  std::unique_ptr<KeyBlobs> derived_key_blobs;
  ASSERT_THAT(RunDerive(kAuthInput, *auth_block_state, derived_key_blobs),
              IsOk());
  ASSERT_TRUE(derived_key_blobs);

  // Assert: verify the derivation gives the same secret as the creation.
  ASSERT_TRUE(created_key_blobs->scrypt_key);
  ASSERT_TRUE(derived_key_blobs->scrypt_key);
  EXPECT_EQ(derived_key_blobs->scrypt_key->derived_key(),
            created_key_blobs->scrypt_key->derived_key());
}

}  // namespace cryptohome
