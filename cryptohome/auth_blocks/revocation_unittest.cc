// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/revocation.h"

#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/backend/pinweaver_manager/pinweaver_manager.h>
#include <libhwsec/error/pinweaver_error.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm_retry_action.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>

#include "cryptohome/pinweaver_manager/mock_le_credential_manager.h"

using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeLECredError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec::TPMError;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace cryptohome {
namespace revocation {

namespace {
const char kFakePerCredentialSecret[] = "fake per-credential secret";
const char kFakeHESecret[] = "fake high entropy secret";
}  // namespace

TEST(RevocationTest, Create) {
  brillo::SecureBlob per_credential_secret(kFakePerCredentialSecret);
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state;
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  EXPECT_CALL(hwsec_pw_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(ReturnValue(/* ret_label */ 0));
  ASSERT_THAT(Create(&hwsec_pw_manager, &le_cred_manager, &state, &key_blobs),
              IsOk());
}

TEST(RevocationTest, Derive) {
  brillo::SecureBlob he_secret(kFakeHESecret);
  brillo::SecureBlob per_credential_secret(kFakePerCredentialSecret);
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  EXPECT_CALL(hwsec_pw_manager, CheckCredential(_, _))
      .WillOnce(ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{
          .he_secret = he_secret}));
  ASSERT_THAT(Derive(&hwsec_pw_manager, &le_cred_manager, state, &key_blobs),
              IsOk());
}

TEST(RevocationTest, DeriveFailsWithoutLabel) {
  brillo::SecureBlob per_credential_secret(kFakePerCredentialSecret);
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  RevocationState state;
  auto status = Derive(&hwsec_pw_manager, &le_cred_manager, state, &key_blobs);
  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(status->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
}

TEST(RevocationTest, Revoke) {
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(hwsec_pw_manager, RemoveCredential(_))
      .WillOnce(DoAll(SaveArg<0>(&label), ReturnOk<hwsec::PinWeaverError>()));
  ASSERT_THAT(Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                     &le_cred_manager, state),
              IsOk());
  EXPECT_EQ(label, state.le_label.value());
}

TEST(RevocationTest, RevokeFailsWithoutLabel) {
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state;
  auto status = Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                       &le_cred_manager, state);
  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(status->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
}

TEST(RevocationTest, RevokeSucceedsWithTPMRetryActionkNoRetry) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(hwsec_pw_manager, RemoveCredential(_))
      .WillOnce(DoAll(SaveArg<0>(&label),
                      ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry)));
  // Revoke succeeds after LE_CRED_ERROR_INVALID_LABEL.
  ASSERT_THAT(Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                     &le_cred_manager, state),
              IsOk());
  EXPECT_EQ(label, state.le_label.value());
}

TEST(RevocationTest, RevokeSucceedsWithTPMRetryActionkSpaceNotFound) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(hwsec_pw_manager, RemoveCredential(_))
      .WillOnce(
          DoAll(SaveArg<0>(&label),
                ReturnError<TPMError>("fake", TPMRetryAction::kSpaceNotFound)));
  // Revoke succeeds after TPMRetryAction::kSpaceNotFound.
  ASSERT_THAT(Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                     &le_cred_manager, state),
              IsOk());
  EXPECT_EQ(label, state.le_label.value());
}

TEST(RevocationTest, RevokeFailsWithTPMRetryActionkReboot) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(hwsec_pw_manager, RemoveCredential(_))
      .WillOnce(DoAll(SaveArg<0>(&label),
                      ReturnError<TPMError>("fake", TPMRetryAction::kReboot)));
  // Revoke fails after TPMRetryAction::kReboot.
  auto status = Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                       &le_cred_manager, state);
  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(status->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
}

TEST(RevocationTest, RevokeFailsWithTPMRetryActionkUserAuth) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(hwsec_pw_manager, RemoveCredential(_))
      .WillOnce(
          DoAll(SaveArg<0>(&label),
                ReturnError<TPMError>("fake", TPMRetryAction::kUserAuth)));
  // Revoke fails after TPMRetryAction::kUserAuth.
  auto status = Revoke(AuthBlockType::kCryptohomeRecovery, &hwsec_pw_manager,
                       &le_cred_manager, state);
  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(status->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
}

}  // namespace revocation
}  // namespace cryptohome
