// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/key_objects.h"

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;

KeyBlobs GetFakeKeyBlobs() {
  return KeyBlobs{
      .vkk_key = brillo::SecureBlob("fake key"),
  };
}

KeyBlobs GetOtherFakeKeyBlobs() {
  return KeyBlobs{
      .vkk_key = brillo::SecureBlob("other fake key"),
  };
}

// Test that `DeriveUssCredentialSecret()` succeeds and returns a nonempty
// result.
TEST(KeyBlobsTest, UssCredentialSecretDerivation) {
  const KeyBlobs key_blobs = GetFakeKeyBlobs();
  const auto uss_credential_secret = key_blobs.DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret, IsOk());
  EXPECT_FALSE(uss_credential_secret->empty());
}

// Test that `DeriveUssCredentialSecret()` returns the same result for the same
// key blobs.
TEST(KeyBlobsTest, UssCredentialSecretDerivationStable) {
  const auto uss_credential_secret_1 =
      GetFakeKeyBlobs().DeriveUssCredentialSecret();
  const auto uss_credential_secret_2 =
      GetFakeKeyBlobs().DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret_1, IsOk());
  ASSERT_THAT(uss_credential_secret_2, IsOk());
  EXPECT_EQ(*uss_credential_secret_1, *uss_credential_secret_2);
}

// Test that `DeriveUssCredentialSecret()` returns different results for
// different key blobs.
TEST(KeyBlobsTest, UssCredentialSecretDerivationNoCollision) {
  const KeyBlobs key_blobs_1 = GetFakeKeyBlobs();
  const auto uss_credential_secret_1 = key_blobs_1.DeriveUssCredentialSecret();

  const KeyBlobs key_blobs_2 = GetOtherFakeKeyBlobs();
  const auto uss_credential_secret_2 = key_blobs_2.DeriveUssCredentialSecret();

  EXPECT_NE(*uss_credential_secret_1, *uss_credential_secret_2);
}

// Test that `DeriveUssCredentialSecret()` fails gracefully for an empty key
// blob.
TEST(KeyBlobsTest, UssCredentialSecretDerivationEmptyFailure) {
  const KeyBlobs clear_key_blobs;
  EXPECT_THAT(clear_key_blobs.DeriveUssCredentialSecret(), NotOk());

  const KeyBlobs empty_key_blobs = {.vkk_key = brillo::SecureBlob()};
  EXPECT_THAT(empty_key_blobs.DeriveUssCredentialSecret(), NotOk());
}

}  // namespace
}  // namespace cryptohome
