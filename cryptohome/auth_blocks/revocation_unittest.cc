// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/revocation.h"

#include <gtest/gtest.h>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_le_credential_manager.h"

using cryptohome::error::CryptohomeLECredError;
using hwsec_foundation::error::testing::ReturnError;
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
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state;
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(ReturnError<CryptohomeLECredError>());
  EXPECT_EQ(CryptoError::CE_NONE, Create(&le_cred_manager, &state, &key_blobs));
}

TEST(RevocationTest, Derive) {
  brillo::SecureBlob he_secret(kFakeHESecret);
  brillo::SecureBlob per_credential_secret(kFakePerCredentialSecret);
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  EXPECT_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(he_secret),
                      ReturnError<CryptohomeLECredError>()));
  EXPECT_EQ(CryptoError::CE_NONE, Derive(&le_cred_manager, state, &key_blobs));
}

TEST(RevocationTest, DeriveFailsWithoutLabel) {
  brillo::SecureBlob per_credential_secret(kFakePerCredentialSecret);
  NiceMock<MockLECredentialManager> le_cred_manager;
  KeyBlobs key_blobs = {.vkk_key = per_credential_secret};
  RevocationState state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            Derive(&le_cred_manager, state, &key_blobs));
}

TEST(RevocationTest, Revoke) {
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state = {.le_label = 0};
  uint64_t label;
  EXPECT_CALL(le_cred_manager, RemoveCredential(_))
      .WillOnce(
          DoAll(SaveArg<0>(&label), ReturnError<CryptohomeLECredError>()));
  EXPECT_EQ(CryptoError::CE_NONE, Revoke(&le_cred_manager, state));
  EXPECT_EQ(label, state.le_label.value());
}

TEST(RevocationTest, RevokeFailsWithoutLabel) {
  NiceMock<MockLECredentialManager> le_cred_manager;
  RevocationState state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO, Revoke(&le_cred_manager, state));
}

}  // namespace revocation
}  // namespace cryptohome
