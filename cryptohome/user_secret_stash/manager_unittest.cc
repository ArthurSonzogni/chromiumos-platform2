// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/manager.h"

#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/file_system_keyset_test_utils.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::status::MakeStatus;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::NiceMock;

class UssManagerTest : public ::testing::Test {
 protected:
  // Utility to create a USS file for the given user with a randomly generated
  // filesystem keyset. This shouldn't ever fail, but we can't use ASSERT_
  // checks in a non-void function and so we return a status or that callers
  // should do an ASSERT_THAT IsOk on.
  CryptohomeStatusOr<FileSystemKeyset> CreateRandomUss(
      ObfuscatedUsername username) {
    UserUssStorage user_storage(storage_, username);
    ASSIGN_OR_RETURN(auto uss,
                     DecryptedUss::CreateWithRandomMainKey(
                         user_storage, FileSystemKeyset::CreateRandom()));
    {
      auto transaction = uss.StartTransaction();
      RETURN_IF_ERROR(transaction.InsertWrappedMainKey(kLabel, kWrappingKey));
      RETURN_IF_ERROR(std::move(transaction).Commit());
    }
    return uss.file_system_keyset();
  }

  // Create a stub not-OK error status.
  CryptohomeStatus CreateNotOkStatus() {
    return MakeStatus<CryptohomeError>(kTestErrorLocation, ErrorActionSet());
  }

  // Fake platform to mediate all the filesystem access.
  FakePlatform platform_;

  // Label and key used to wrap randomly created USS instances.
  const std::string kLabel = "key";
  const brillo::SecureBlob kWrappingKey =
      brillo::SecureBlob(kAesGcm256KeySize, 0xA);
  // An additional key which is wrong as should not work.
  const brillo::SecureBlob kBadWrappingKey =
      brillo::SecureBlob(kAesGcm256KeySize, 0xB);

  // A couple of standard usernames to use in tests.
  const ObfuscatedUsername kUser1 =
      SanitizeUserName(Username("foo@example.com"));
  const ObfuscatedUsername kUser2 =
      SanitizeUserName(Username("bar@example.com"));

  // A fake error location for use in tests that need to make a not-OK status.
  const CryptohomeError::ErrorLocationPair kTestErrorLocation =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("FakeErrorLocationForUssManagerTest"));

  // Mockable storage to be managed.
  UssStorage storage_{&platform_};
};

TEST_F(UssManagerTest, NoUssFilesLoadEncryptedFails) {
  UssManager uss_manager(storage_);

  EXPECT_THAT(uss_manager.LoadEncrypted(kUser1), NotOk());
  EXPECT_THAT(uss_manager.LoadEncrypted(kUser2), NotOk());
}

TEST_F(UssManagerTest, CreateAndLoadEncryptedFile) {
  UssManager uss_manager(storage_);

  // Initially loading should fail.
  EXPECT_THAT(uss_manager.LoadEncrypted(kUser1), NotOk());

  // Now create a random USS for the user and check that we can load it.
  ASSERT_THAT(CreateRandomUss(kUser1), IsOk());
  auto uss1 = uss_manager.LoadEncrypted(kUser1);
  ASSERT_THAT(uss1, IsOk());
  EXPECT_THAT((*uss1)->WrappedMainKeyIds(), ElementsAre(kLabel));

  // Now rewrite the USS with a new random one, loading it should ignore then
  // and just return the already loaded instance.
  ASSERT_THAT(CreateRandomUss(kUser1), IsOk());
  auto uss2 = uss_manager.LoadEncrypted(kUser1);
  EXPECT_THAT(*uss1, Eq(*uss2));
}

TEST_F(UssManagerTest, NoUssFilesLoadDecryptedFails) {
  UssManager uss_manager(storage_);

  EXPECT_THAT(uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey), NotOk());
  EXPECT_THAT(uss_manager.LoadDecrypted(kUser2, kLabel, kWrappingKey), NotOk());
}

TEST_F(UssManagerTest, CreateAndAddDecryptedFile) {
  UssManager uss_manager(storage_);
  UserUssStorage user_storage(storage_, kUser1);

  // Create a random USS.
  auto created_uss = DecryptedUss::CreateWithRandomMainKey(
      user_storage, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(created_uss, IsOk());
  FileSystemKeyset fsk = created_uss->file_system_keyset();

  // We should be able to add it and then retrieve the USS again.
  auto created_token =
      uss_manager.AddDecrypted(kUser1, *std::move(created_uss));
  ASSERT_THAT(created_token, IsOk());

  // Now get the instance using the token, it should be the same.
  DecryptedUss& gotten_uss = uss_manager.GetDecrypted(*created_token);
  EXPECT_THAT(gotten_uss.file_system_keyset(), FileSystemKeysetEquals(fsk));
}

TEST_F(UssManagerTest, CreateAndAddDecryptedFileFailsOnEncryptedCollision) {
  UssManager uss_manager(storage_);
  UserUssStorage user_storage(storage_, kUser1);

  // Create a random USS and load the encrypted version.
  ASSERT_THAT(CreateRandomUss(kUser1), IsOk());
  auto uss = uss_manager.LoadEncrypted(kUser1);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT((*uss)->WrappedMainKeyIds(), ElementsAre(kLabel));

  // Now create a new USS and attempt to add it. It should fail.
  auto created_uss = DecryptedUss::CreateWithRandomMainKey(
      user_storage, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(created_uss, IsOk());
  EXPECT_THAT(uss_manager.AddDecrypted(kUser1, *std::move(created_uss)),
              NotOk());
}

TEST_F(UssManagerTest, CreateAndAddDecryptedFileFailsOnDecryptedCollision) {
  UssManager uss_manager(storage_);
  UserUssStorage user_storage(storage_, kUser1);

  // Create a random USS and load the decrypted version.
  auto fsk = CreateRandomUss(kUser1);
  ASSERT_THAT(fsk, IsOk());
  auto token = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(token, IsOk());
  DecryptedUss& uss = uss_manager.GetDecrypted(*token);
  EXPECT_THAT(uss.file_system_keyset(), FileSystemKeysetEquals(*fsk));

  // Now create a new USS and attempt to add it. It should fail.
  auto created_uss = DecryptedUss::CreateWithRandomMainKey(
      user_storage, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(created_uss, IsOk());
  EXPECT_THAT(uss_manager.AddDecrypted(kUser1, *std::move(created_uss)),
              NotOk());
}

TEST_F(UssManagerTest, CreateAndLoadDecryptedFile) {
  UssManager uss_manager(storage_);

  // Initially loading should fail.
  EXPECT_THAT(uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey), NotOk());

  // Now create a random USS for the user and check that we can load it.
  auto fsk = CreateRandomUss(kUser1);
  ASSERT_THAT(fsk, IsOk());

  // Now load the instance.
  auto token1 = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(token1, IsOk());
  DecryptedUss& uss1 = uss_manager.GetDecrypted(*token1);
  EXPECT_THAT(uss1.file_system_keyset(), FileSystemKeysetEquals(*fsk));

  // Now load the decrypted instance again. We should get a new token but it
  // should access the same underlying object.
  auto token2 = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(token2, IsOk());
  DecryptedUss& uss2 = uss_manager.GetDecrypted(*token2);
  EXPECT_THAT(uss2.file_system_keyset(), FileSystemKeysetEquals(*fsk));
  EXPECT_THAT(&uss1, Eq(&uss2));
}

TEST_F(UssManagerTest, CannotLoadDecryptedWithBadKey) {
  UssManager uss_manager(storage_);

  // Initially loading should fail.
  EXPECT_THAT(uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey), NotOk());

  // Now create a random USS for the user.
  auto fsk = CreateRandomUss(kUser1);
  ASSERT_THAT(fsk, IsOk());

  // Try to load the instance with a bad key, it should fail.
  auto bad_token1 = uss_manager.LoadDecrypted(kUser1, kLabel, kBadWrappingKey);
  ASSERT_THAT(bad_token1, NotOk());

  // Now load the instance with a good key.
  auto token = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(token, IsOk());
  DecryptedUss& uss = uss_manager.GetDecrypted(*token);
  EXPECT_THAT(uss.file_system_keyset(), FileSystemKeysetEquals(*fsk));

  // Now try to use a bad key again. It should fail even though the manager
  // already has a copy of the decrypted USS loaded.
  auto bad_token2 = uss_manager.LoadDecrypted(kUser1, kLabel, kBadWrappingKey);
  ASSERT_THAT(bad_token2, NotOk());
}

TEST_F(UssManagerTest, DecryptTokensActAsRefcount) {
  UssManager uss_manager(storage_);

  // Create two random USS objects.
  ASSERT_THAT(CreateRandomUss(kUser1), IsOk());
  ASSERT_THAT(CreateRandomUss(kUser2), IsOk());

  // Create several tokens for each user.
  auto u1_token1 = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(u1_token1, IsOk());
  auto u1_token2 = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(u1_token2, IsOk());
  auto u1_token3 = uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(u1_token3, IsOk());
  auto u2_token1 = uss_manager.LoadDecrypted(kUser2, kLabel, kWrappingKey);
  ASSERT_THAT(u2_token1, IsOk());
  auto u2_token2 = uss_manager.LoadDecrypted(kUser2, kLabel, kWrappingKey);
  ASSERT_THAT(u2_token2, IsOk());

  // We should not be able to discard any users.
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), NotOk());
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser2), NotOk());
  EXPECT_THAT(uss_manager.DiscardAllEncrypted(), NotOk());

  // Now delete some of the tokens. We should be able to discard user2 but not
  // user1 because we still have a user 1 token. Use a couple different ways of
  // blanking the token to exercise both move-assign and destroy.
  u1_token1 = CreateNotOkStatus();
  u1_token2 = UssManager::DecryptToken();
  u2_token1 = CreateNotOkStatus();
  u2_token2 = UssManager::DecryptToken();
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), NotOk());
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser2), IsOk());

  // Move the remaining token. The DecryptedUss should still be live.
  UssManager::DecryptToken moved_token = std::move(*u1_token3);
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), NotOk());

  // Now if we finally delete the last token we should be able to remove the
  // encrypted for user1.
  moved_token = UssManager::DecryptToken();
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), IsOk());

  // Load decrypted again, it should still work.
  auto reloaded_u1_token =
      uss_manager.LoadDecrypted(kUser1, kLabel, kWrappingKey);
  ASSERT_THAT(reloaded_u1_token, IsOk());
  auto reloaded_u2_token =
      uss_manager.LoadDecrypted(kUser2, kLabel, kWrappingKey);
  ASSERT_THAT(reloaded_u2_token, IsOk());
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), NotOk());
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser2), NotOk());
  EXPECT_THAT(uss_manager.DiscardAllEncrypted(), NotOk());

  // Now drop the tokens again, we should be able to discard everything.
  reloaded_u1_token = CreateNotOkStatus();
  reloaded_u2_token = CreateNotOkStatus();
  EXPECT_THAT(uss_manager.DiscardAllEncrypted(), IsOk());

  // More discards after everything is discarded should work.
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser1), IsOk());
  EXPECT_THAT(uss_manager.DiscardEncrypted(kUser2), IsOk());
  EXPECT_THAT(uss_manager.DiscardAllEncrypted(), IsOk());
}

}  // namespace
}  // namespace cryptohome
