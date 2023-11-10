// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/decrypted.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <base/test/scoped_chromeos_version_info.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/file_system_keyset_test_utils.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/storage.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::AesGcmEncrypt;
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

UserSecretStashPayload CreateBasicUssPayload() {
  FileSystemKeyset file_system_keyset = CreateTestFsk();
  // Create a basic payload with the filesystem keys.
  UserSecretStashPayload payload = {
      .fek = file_system_keyset.Key().fek,
      .fnek = file_system_keyset.Key().fnek,
      .fek_salt = file_system_keyset.Key().fek_salt,
      .fnek_salt = file_system_keyset.Key().fnek_salt,
      .fek_sig = file_system_keyset.KeyReference().fek_sig,
      .fnek_sig = file_system_keyset.KeyReference().fnek_sig,
      .chaps_key = file_system_keyset.chaps_key(),
  };
  return payload;
}

std::optional<EncryptedUss::Container> CreateUssContainer(
    const brillo::SecureBlob& main_key, const UserSecretStashPayload& payload) {
  EncryptedUss::Container container;

  // Serialize and then encrypt the payload.
  auto serialized_payload = payload.Serialize();
  if (!serialized_payload) {
    return std::nullopt;
  }
  brillo::Blob iv, tag, ciphertext;
  if (!AesGcmEncrypt(serialized_payload.value(), /*ad=*/std::nullopt, main_key,
                     &iv, &tag, &ciphertext)) {
    return std::nullopt;
  }

  // Copy the resulting encrypted output into the container.
  container.ciphertext = std::move(ciphertext);
  container.iv = std::move(iv);
  container.gcm_tag = std::move(tag);

  return container;
}

// Matcher that checks if a given secure blob is NOT contained in the specified
// blob. This is useful for making sure that a secret plaintext is not showing
// up in a (supposedly encrypted) ciphertext.
class IsNotInBlobMatcher {
 public:
  using is_gtest_matcher = void;

  explicit IsNotInBlobMatcher(brillo::Blob blob) : blob_(std::move(blob)) {}

  bool MatchAndExplain(const brillo::SecureBlob& secure_blob,
                       std::ostream*) const {
    return std::search(blob_.begin(), blob_.end(), secure_blob.begin(),
                       secure_blob.end()) == blob_.end();
  }

  void DescribeTo(std::ostream* os) const { *os << "is not contained in blob"; }
  void DescribeNegationTo(std::ostream* os) const {
    *os << "is contained in blob";
  }

 private:
  brillo::Blob blob_;
};
::testing::Matcher<brillo::SecureBlob> IsNotInBlob(brillo::Blob blob) {
  return IsNotInBlobMatcher(std::move(blob));
}

class DecryptedUssTest : public ::testing::Test {
 protected:
  const ObfuscatedUsername kObfuscatedUsername{"foo@gmail.com"};

  FakePlatform platform_;
  UssStorage uss_storage_{&platform_};
  UserUssStorage user_uss_storage_{uss_storage_, kObfuscatedUsername};
};

TEST_F(DecryptedUssTest, AddWrappedKeys) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, AddWrappedKeysAndCommitToStorage) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

  // Check that the changes are persisted to the storage.
  auto encrypted_uss = EncryptedUss::FromStorage(user_uss_storage_);
  ASSERT_THAT(encrypted_uss, IsOk());
  EXPECT_THAT(encrypted_uss->WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
}

TEST_F(DecryptedUssTest, InsertWrappedKeyRejectsDuplicates) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, AssignWrappedKeyOverwritesDuplicates) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, RenameWrappedKeySuccess) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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
    EXPECT_THAT(transaction.RenameWrappingId("a", "c"), IsOk());
    EXPECT_THAT(transaction.RenameWrappingId("b", "d"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("c", "d"));
}

TEST_F(DecryptedUssTest, RenameWrappedKeyWithResetSecret) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Set up some initial wrapped keys with secrets.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xE);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xF);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("b", kSecret2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Optional(kSecret1));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Optional(kSecret2));

  // Rename both of them.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.RenameWrappingId("a", "c"), IsOk());
    EXPECT_THAT(transaction.RenameWrappingId("b", "d"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("c", "d"));
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Eq(std::nullopt));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Eq(std::nullopt));
  EXPECT_THAT(decrypted_uss->GetResetSecret("c"), Optional(kSecret1));
  EXPECT_THAT(decrypted_uss->GetResetSecret("d"), Optional(kSecret2));
}

TEST_F(DecryptedUssTest, RenameWrappedKeyFailures) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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
    EXPECT_THAT(transaction.RenameWrappingId("a", "b"), NotOk());
    EXPECT_THAT(transaction.RenameWrappingId("c", "d"), NotOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
}

TEST_F(DecryptedUssTest, RemoveWrappedKey) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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
    EXPECT_THAT(transaction.RemoveWrappingId("c"), NotOk());
    EXPECT_THAT(transaction.RemoveWrappingId("a"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("b"));
}

TEST_F(DecryptedUssTest, RemoveWrappedKeyWithResetSecret) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Set up some initial wrapped keys with secrets.
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xB);
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xE);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xF);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", kWrappingKey1), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.InsertWrappedMainKey("b", kWrappingKey2), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("b", kSecret2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("a", "b"));
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Optional(kSecret1));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Optional(kSecret2));

  // Remove one element, and try to remove an element that doesn't exist.
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.RemoveWrappingId("c"), NotOk());
    EXPECT_THAT(transaction.RemoveWrappingId("a"), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              UnorderedElementsAre("b"));
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Eq(std::nullopt));
  EXPECT_THAT(decrypted_uss->GetResetSecret("b"), Optional(kSecret2));
}

TEST_F(DecryptedUssTest, AddResetSecrets) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, InsertResetSecretRejectsDuplicates) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, AssignResetSecretAllowsDuplicates) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());

  // Check for the initial secrets.
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Eq(std::nullopt));

  // Inject some secrets.
  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xA);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xB);
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.AssignResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.AssignResetSecret("a", kSecret2), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }
  EXPECT_THAT(decrypted_uss->GetResetSecret("a"), Optional(kSecret2));
}

TEST_F(DecryptedUssTest, AddRateLimiterResetSecrets) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, InsertRateLimiterResetSecretRejectsDuplicates) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, FingerprintRateLimiterId) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, TransactionsIgnoredWithoutCommit) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
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

TEST_F(DecryptedUssTest, CreateFailsWithEmptyMainKey) {
  brillo::SecureBlob empty_main_key;
  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), empty_main_key);
  EXPECT_THAT(decrypted_uss, NotOk());
}

TEST_F(DecryptedUssTest, CreateFailsWithShortMainKey) {
  brillo::SecureBlob short_main_key(kAesGcm256KeySize / 2, 0xA);
  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), short_main_key);
  EXPECT_THAT(decrypted_uss, NotOk());
}

TEST_F(DecryptedUssTest, CreateToBlobWorksWithFromStorage) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
  ASSERT_THAT(decrypted_uss->StartTransaction().Commit(), IsOk());

  auto redecrypted_uss =
      DecryptedUss::FromStorageUsingMainKey(user_uss_storage_, main_key);
  ASSERT_THAT(redecrypted_uss, IsOk());
  EXPECT_THAT(redecrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
}

TEST_F(DecryptedUssTest, CreateToBlobWorksWithFromStorageWithWrapping) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);
  brillo::SecureBlob wrapping_key(kAesGcm256KeySize, 0xB);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", wrapping_key), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }

  auto redecrypted_uss = DecryptedUss::FromStorageUsingWrappedKey(
      user_uss_storage_, "a", wrapping_key);
  ASSERT_THAT(redecrypted_uss, IsOk());
  EXPECT_THAT(redecrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
}

TEST_F(DecryptedUssTest, FromStorageFailsWithBadKey) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);
  brillo::SecureBlob wrong_key(kAesGcm256KeySize, 0xB);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));

  auto redecrypted_uss =
      DecryptedUss::FromStorageUsingMainKey(user_uss_storage_, wrong_key);
  ASSERT_THAT(redecrypted_uss, NotOk());
}

// Do a basic check that the secret parts of USS don't just show up in the clear
// in the encrypted blob. This isn't perfect because of course you could have
// the secrets poorly encrypted in the output but there's no test for that.
TEST_F(DecryptedUssTest, NoSecretsInEncryptedBlob) {
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);
  brillo::SecureBlob wrapping_key(kAesGcm256KeySize, 0xB);

  const brillo::SecureBlob kSecret1(kAesGcm256KeySize, 0xD);
  const brillo::SecureBlob kSecret2(kAesGcm256KeySize, 0xE);
  const brillo::SecureBlob kSecret3(kAesGcm256KeySize, 0xF);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_THAT(decrypted_uss->file_system_keyset(),
              FileSystemKeysetEquals(CreateTestFsk()));
  {
    auto transaction = decrypted_uss->StartTransaction();
    EXPECT_THAT(transaction.InsertWrappedMainKey("a", wrapping_key), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("a", kSecret1), IsOk());
    EXPECT_THAT(transaction.InsertResetSecret("b", kSecret2), IsOk());
    EXPECT_THAT(transaction.InsertRateLimiterResetSecret(
                    AuthFactorType::kPassword, kSecret3),
                IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
  }

  auto blob = decrypted_uss->encrypted().ToBlob();
  ASSERT_THAT(blob, IsOk());

  EXPECT_THAT(CreateTestFsk().Key().fek, IsNotInBlob(*blob));
  EXPECT_THAT(kSecret1, IsNotInBlob(*blob));
  EXPECT_THAT(kSecret2, IsNotInBlob(*blob));
  EXPECT_THAT(kSecret3, IsNotInBlob(*blob));
}

TEST_F(DecryptedUssTest, OsVersion) {
  constexpr char kLsbRelease[] =
      "CHROMEOS_RELEASE_NAME=Chrome "
      "OS\nCHROMEOS_RELEASE_VERSION=11012.0.2018_08_28_1422\n";
  base::test::ScopedChromeOSVersionInfo scoped_version(
      kLsbRelease, /*lsb_release_time=*/base::Time());
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());

  EXPECT_THAT(decrypted_uss->encrypted().created_on_os_version(),
              Eq("11012.0.2018_08_28_1422"));
}

TEST_F(DecryptedUssTest, OsVersionStays) {
  constexpr char kLsbRelease1[] =
      "CHROMEOS_RELEASE_NAME=Chrome "
      "OS\nCHROMEOS_RELEASE_VERSION=11012.0.2018_08_28_1422\n";
  constexpr char kLsbRelease2[] =
      "CHROMEOS_RELEASE_NAME=Chrome "
      "OS\nCHROMEOS_RELEASE_VERSION=22222.0.2028_01_01_9999\n";
  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  // Create the USS on version 1.
  {
    base::test::ScopedChromeOSVersionInfo scoped_version1(
        kLsbRelease1, /*lsb_release_time=*/base::Time());

    auto decrypted_uss = DecryptedUss::CreateWithMainKey(
        user_uss_storage_, CreateTestFsk(), main_key);
    ASSERT_THAT(decrypted_uss, IsOk());
    ASSERT_THAT(decrypted_uss->StartTransaction().Commit(), IsOk());
  }

  // Decrypt the USS on the version 2. The field should be the release 1 version
  // even if the version at decrypt time was version 2.
  {
    base::test::ScopedChromeOSVersionInfo scoped_version2(
        kLsbRelease2, /*lsb_release_time=*/base::Time());

    auto decrypted_uss =
        DecryptedUss::FromStorageUsingMainKey(user_uss_storage_, main_key);
    ASSERT_THAT(decrypted_uss, IsOk());

    EXPECT_THAT(decrypted_uss->encrypted().created_on_os_version(),
                Eq("11012.0.2018_08_28_1422"));
  }
}

TEST_F(DecryptedUssTest, MissingOsVersion) {
  // Note: Normally unit tests don't have access to a CrOS /etc/lsb-release
  // anyway, but this override guarantees that the test passes regardless of
  // that.
  base::test::ScopedChromeOSVersionInfo scoped_version(
      /*lsb_release=*/"", /*lsb_release_time=*/base::Time());

  brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  auto decrypted_uss = DecryptedUss::CreateWithMainKey(
      user_uss_storage_, CreateTestFsk(), main_key);
  ASSERT_THAT(decrypted_uss, IsOk());

  EXPECT_THAT(decrypted_uss->encrypted().created_on_os_version(), IsEmpty());
}

TEST_F(DecryptedUssTest, CreatedUssContainsKeyDerivationSeed) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_FALSE(decrypted_uss->key_derivation_seed().empty());
}

TEST_F(DecryptedUssTest, KeyDerivationSeedBackfilled) {
  const brillo::SecureBlob main_key(kAesGcm256KeySize, 0xA);

  UserSecretStashPayload payload = CreateBasicUssPayload();
  // Ensure that the basic payload doesn't contain the |key_derivation_seed|
  // field, such that the backfill logic is tested.
  EXPECT_TRUE(payload.key_derivation_seed.empty());
  std::optional<EncryptedUss::Container> uss_container =
      CreateUssContainer(main_key, payload);
  ASSERT_THAT(uss_container, Optional(_));
  ASSERT_THAT(EncryptedUss(*uss_container).ToStorage(user_uss_storage_),
              IsOk());
  auto decrypted_uss =
      DecryptedUss::FromStorageUsingMainKey(user_uss_storage_, main_key);
  ASSERT_THAT(decrypted_uss, IsOk());
  EXPECT_FALSE(decrypted_uss->key_derivation_seed().empty());
}

TEST_F(DecryptedUssTest, GetSecurityDomainKeys) {
  auto decrypted_uss =
      DecryptedUss::CreateWithRandomMainKey(user_uss_storage_, CreateTestFsk());
  ASSERT_THAT(decrypted_uss, IsOk());
  const auto* keys = decrypted_uss->GetSecurityDomainKeys();
  EXPECT_NE(keys, nullptr);
}

}  // namespace
}  // namespace cryptohome
