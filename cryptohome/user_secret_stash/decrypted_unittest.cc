// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/decrypted.h"

#include <optional>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/file_system_keyset_test_utils.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;

// Create a fake file system keyset for use in all tests. The contents are not
// actual keys but they should be sufficient for us to verify that the keyset
// that went into the USS is what came back out.
FileSystemKeyset CreateTestFsk() {
  return FileSystemKeyset(
      FileSystemKey{
          .fek = brillo::SecureBlob("fek"),
          .fnek = brillo::SecureBlob("fnek"),
          .fek_salt = brillo::SecureBlob("fek-salt"),
          .fnek_salt = brillo::SecureBlob("fnek-salt"),
      },
      FileSystemKeyReference{
          .fek_sig = brillo::SecureBlob("fek-sig"),
          .fnek_sig = brillo::SecureBlob("fnek-sig"),
      },
      brillo::SecureBlob("chaps-key"));
}

TEST(DecryptedUssTest, AddWrappedKeys) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check the initial value.
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(), IsEmpty());

  // Inject some wrapped keys.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
}

TEST(DecryptedUssTest, InsertWrappedKeyRejectsDuplicates) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check the initial value.
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(), IsEmpty());

  // Insert a key twice using insert.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey2), NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a"));
}

TEST(DecryptedUssTest, AssignWrappedKeyOverwritesDuplicates) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check the initial value.
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(), IsEmpty());

  // Assign a key twice using assign.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.AssignWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.AssignWrappedMainKey("a", kWrappingKey2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a"));
}

TEST(DecryptedUssTest, RenameWrappedKeySuccess) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Set up some initial wrapped keys.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));

  // Rename both of them.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.RenameWrappedMainKey("a", "c"), IsOk());
    EXPECT_THAT(transaction.RenameWrappedMainKey("b", "d"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("c", "d"));
}

TEST(DecryptedUssTest, RenameWrappedKeyFailures) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Set up some initial wrapped keys.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));

  // Try to rename them in ways that would fail.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.RenameWrappedMainKey("a", "b"), NotOk());
    EXPECT_THAT(transaction.RenameWrappedMainKey("c", "d"), NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
}

TEST(DecryptedUssTest, RemoveWrappedKey) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Set up some initial wrapped keys.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));

  // Remove one element, and try to remove an element that doesn't exist.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.RemoveWrappedMainKey("c"), NotOk());
    EXPECT_THAT(transaction.RemoveWrappedMainKey("a"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("b"));
}

TEST(DecryptedUssTest, AddResetSecrets) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check for the initial secrets.
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Eq(std::nullopt));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Eq(std::nullopt));

  // Inject some secrets.
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("b", kSecret2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Optional(kSecret1));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Optional(kSecret2));
}

TEST(DecryptedUssTest, InsertResetSecretRejectsDuplicates) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check for the initial secrets.
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Eq(std::nullopt));

  // Inject some secrets.
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret2), NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Optional(kSecret1));
}

TEST(DecryptedUssTest, AddRateLimiterResetSecrets) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check for the initial secrets.
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kPassword),
      Eq(std::nullopt));
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kFingerprint),
      Eq(std::nullopt));

  // Inject some secrets.
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertRateLimiterResetSecret(
                    AuthFactorType::kPassword, kSecret1),
                IsOk());
    EXPECT_THAT(transaction.InsertRateLimiterResetSecret(
                    AuthFactorType::kFingerprint, kSecret2),
                IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kPassword),
      Optional(kSecret1));
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kFingerprint),
      Optional(kSecret2));
}

TEST(DecryptedUssTest, InsertRateLimiterResetSecretRejectsDuplicates) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check for the initial secrets.
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kPassword),
      Eq(std::nullopt));
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kFingerprint),
      Eq(std::nullopt));

  // Inject some secrets.
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertRateLimiterResetSecret(
                    AuthFactorType::kPassword, kSecret1),
                IsOk());
    EXPECT_THAT(transaction.InsertRateLimiterResetSecret(
                    AuthFactorType::kPassword, kSecret2),
                NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(
      decrypted_uss->GetRateLimiterResetSecret(AuthFactorType::kPassword),
      Optional(kSecret1));
}

TEST(DecryptedUssTest, FingerprintRateLimiterId) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check the initial value.
  EXPECT_THAT(decrypted_uss->encrypted().fingerprint_rate_limiter_id(),
              Eq(std::nullopt));

  // Initialize the rate limiter ID.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InitializeFingerprintRateLimiterId(1234), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().fingerprint_rate_limiter_id(),
              Optional(1234));

  // Attempt to initialize it a second time.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InitializeFingerprintRateLimiterId(5678), NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().fingerprint_rate_limiter_id(),
              Optional(1234));
}

TEST(DecryptedUssTest, TransactionsIgnoredWithoutCommit) {
  auto decrypted_uss = DecryptedUss::CreateWithRandomMainKey(CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  EXPECT_THAT(decrypted_uss->encrypted().fingerprint_rate_limiter_id(),
              Eq(std::nullopt));
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InitializeFingerprintRateLimiterId(1234), IsOk());
    // No commit, just discard the transaction.
  }
  EXPECT_THAT(decrypted_uss->encrypted().fingerprint_rate_limiter_id(),
              Eq(std::nullopt));
}

TEST(DecryptedUssTest, CreateFailsWithEmptyMainKey) {
  brillo::SecureBlob empty_main_key;
  auto decrypted_uss =
      DecryptedUss::CreateWithMainKey(CreateTestFsk(), empty_main_key);
  EXPECT_THAT(decrypted_uss, NotOk());
}

TEST(DecryptedUssTest, CreateToBlobWorksWithFromBlob) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  auto decrypted_uss =
      DecryptedUss::CreateWithMainKey(CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));

  auto blob = decrypted_uss->encrypted().ToBlob();
  ASSERT_THAT(blob, IsOk());

  auto redecrypted_uss = DecryptedUss::FromBlobUsingMainKey(*blob, main_key);
  ASSERT_THAT(redecrypted_uss, IsOk());
  EXPECT_THAT(redecrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
}

TEST(DecryptedUssTest, CreateToBlobWorksWithFromBlobWithWrapping) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);
  brillo::SecureBlob wrapping_key(kAesGcm256KeySize, 0xB);

  auto decrypted_uss =
      DecryptedUss::CreateWithMainKey(CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", wrapping_key), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }

  auto blob = decrypted_uss->encrypted().ToBlob();
  ASSERT_THAT(blob, IsOk());

  auto redecrypted_uss =
      DecryptedUss::FromBlobUsingWrappedKey(*blob, "a", wrapping_key);
  ASSERT_THAT(redecrypted_uss, IsOk());
  EXPECT_THAT(redecrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
}

}  // namespace
}  // namespace cryptohome
