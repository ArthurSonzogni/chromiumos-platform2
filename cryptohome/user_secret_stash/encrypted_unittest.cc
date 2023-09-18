// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/encrypted.h"

#include <optional>

#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::brillo::cryptohome::home::SanitizeUserName;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::_;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;

// Helper function that creates a flatbuffer container populated with values.
// Note that all of the ciphertext and keys are made up, and so can't actually
// be decrypted.
UserSecretStashContainer MakeFlatbufferForTest() {
  return {
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .ciphertext = brillo::BlobFromString("encrypted bytes!"),
      .iv = brillo::BlobFromString(std::string(kAesGcmIVSize, '\x0a')),
      .gcm_tag = brillo::BlobFromString(std::string(kAesGcmTagSize, '\x0b')),
      .wrapped_key_blocks =
          {
              {
                  .wrapping_id = "password",
                  .encryption_algorithm =
                      UserSecretStashEncryptionAlgorithm::AES_GCM_256,
                  .encrypted_key = brillo::BlobFromString("encrypted pass!"),
                  .iv = brillo::BlobFromString(
                      std::string(kAesGcmIVSize, '\x0c')),
                  .gcm_tag = brillo::BlobFromString(
                      std::string(kAesGcmTagSize, '\x0d')),
              },
              {
                  .wrapping_id = "pin",
                  .encryption_algorithm =
                      UserSecretStashEncryptionAlgorithm::AES_GCM_256,
                  .encrypted_key = brillo::BlobFromString("encrypted pin!"),
                  .iv = brillo::BlobFromString(
                      std::string(kAesGcmIVSize, '\x0e')),
                  .gcm_tag = brillo::BlobFromString(
                      std::string(kAesGcmTagSize, '\x0f')),
              },
          },
      .created_on_os_version = "1.2.3.4",
      .user_metadata = {},
  };
}

TEST(EncryptedUssTest, FromEmptyBlob) {
  brillo::Blob empty;

  EXPECT_THAT(EncryptedUss::FromBlob(empty), NotOk());
}

TEST(EncryptedUssTest, FromMissingFile) {
  NiceMock<MockPlatform> platform;
  UssStorage uss_storage{&platform};
  UserUssStorage user_uss_storage{
      uss_storage, SanitizeUserName(Username("user@example.com"))};

  // No file has been set up so this should fail.
  EXPECT_THAT(EncryptedUss::FromStorage(user_uss_storage), NotOk());
}

TEST(EncryptedUssTest, FromValidFile) {
  NiceMock<MockPlatform> platform;
  UssStorage uss_storage{&platform};
  UserUssStorage user_uss_storage{
      uss_storage, SanitizeUserName(Username("user@example.com"))};

  // Construct a flatbuffer and write it out.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));
  auto blob_uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(blob_uss, IsOk());
  ASSERT_THAT(blob_uss->ToStorage(user_uss_storage), IsOk());

  // The test flatbuffer should be loadable.
  auto storage_uss = EncryptedUss::FromStorage(user_uss_storage);
  ASSERT_THAT(storage_uss, IsOk());
  EXPECT_THAT(storage_uss->WrappedMainKeyIds(),
              UnorderedElementsAre("password", "pin"));
  EXPECT_THAT(storage_uss->created_on_os_version(), Eq("1.2.3.4"));
  EXPECT_THAT(storage_uss->fingerprint_rate_limiter_id(), Eq(std::nullopt));
}

TEST(EncryptedUssTest, ToStorageFails) {
  NiceMock<MockPlatform> platform;
  UssStorage uss_storage{&platform};
  UserUssStorage user_uss_storage{
      uss_storage, SanitizeUserName(Username("user@example.com"))};

  // Disable all writes.
  EXPECT_CALL(platform, WriteFileAtomicDurable(_, _, _))
      .WillRepeatedly(Return(false));

  // Construct a flatbuffer attempt write it out.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));
  auto blob_uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(blob_uss, IsOk());
  ASSERT_THAT(blob_uss->ToStorage(user_uss_storage), NotOk());
}

}  // namespace
}  // namespace cryptohome
