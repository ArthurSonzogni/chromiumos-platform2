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

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/file_system_keyset_test_utils.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
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

  // Mockable storage to be managed.
  NiceMock<MockPlatform> platform_;
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

}  // namespace
}  // namespace cryptohome
