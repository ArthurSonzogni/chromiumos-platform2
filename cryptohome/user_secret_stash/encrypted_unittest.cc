// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/encrypted.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

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
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::IsOkAndHolds;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::UnorderedElementsAre;

// The plaintext of the payload that we store in the text flatbuffer. This is
// just readable text and not a proper serialized Payload object with keysets
// and the like, but since EncryptedUss doesn't care about the payload format
// this is an easier thing to compare against.
brillo::SecureBlob MakePayloadForTest() {
  return brillo::SecureBlob("The Big Secret!");
}

// The standard keys used for all of the default flatbuffers. There's a main key
// and wrapping keys for the password and pin blocks.
brillo::SecureBlob MakeMainKeyForTest() {
  return brillo::SecureBlob(kAesGcm256KeySize, '\x0a');
}
brillo::SecureBlob MakePasswordKeyForTest() {
  return brillo::SecureBlob(kAesGcm256KeySize, '\x0b');
}
brillo::SecureBlob MakePinKeyForTest() {
  return brillo::SecureBlob(kAesGcm256KeySize, '\x0c');
}

// Helper function that creates a flatbuffer container populated with values.
// All of the ciphertext was encrypted with the Make*KeyForTest() functions.
UserSecretStashContainer MakeFlatbufferForTest() {
  UserSecretStashContainer container{
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .wrapped_key_blocks =
          {
              {
                  .wrapping_id = "password",
                  .encryption_algorithm =
                      UserSecretStashEncryptionAlgorithm::AES_GCM_256,
              },
              {
                  .wrapping_id = "pin",
                  .encryption_algorithm =
                      UserSecretStashEncryptionAlgorithm::AES_GCM_256,
              },
          },
      .created_on_os_version = "1.2.3.4",
      .user_metadata = {},
  };

  brillo::SecureBlob plaintext = MakePayloadForTest();
  EXPECT_THAT(
      AesGcmEncrypt(plaintext, std::nullopt, MakeMainKeyForTest(),
                    &container.iv, &container.gcm_tag, &container.ciphertext),
      IsTrue());

  auto& password_block = container.wrapped_key_blocks[0];
  EXPECT_THAT(
      AesGcmEncrypt(MakeMainKeyForTest(), std::nullopt,
                    MakePasswordKeyForTest(), &password_block.iv,
                    &password_block.gcm_tag, &password_block.encrypted_key),
      IsTrue());

  auto& pin_block = container.wrapped_key_blocks[1];
  EXPECT_THAT(AesGcmEncrypt(MakeMainKeyForTest(), std::nullopt,
                            MakePinKeyForTest(), &pin_block.iv,
                            &pin_block.gcm_tag, &pin_block.encrypted_key),
              IsTrue());

  return container;
}

// Helper to "corrupt" a blob. The corruption is implemented by just flipping
// all of the bits in the blob. Useful for testing that the code can handle bad
// input data.
void CorruptBlob(brillo::Blob& blob) {
  for (uint8_t& byte : blob) {
    byte ^= 0xff;
  }
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

TEST(EncryptedUssTest, FromCorruptedFile) {
  NiceMock<MockPlatform> platform;
  UssStorage uss_storage{&platform};
  UserUssStorage user_uss_storage{
      uss_storage, SanitizeUserName(Username("user@example.com"))};

  // Construct a flatbuffer.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // Corrupt the entire file contents.
  CorruptBlob(*flatbuffer);

  // The corrupted flatbuffer should not be loadable.
  auto storage_uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(storage_uss, NotOk());
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

TEST(EncryptedUssCorruptionTest, SmokeTest) {
  // Verify that the decrypt of the unmodified container works. The other
  // corruption tests will then make tweaks to the container to break things in
  // interesting ways, but it only makes sense if this test passes.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can decrypt the container and unwrap both main keys.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->DecryptPayload(MakeMainKeyForTest()), IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("password", MakePasswordKeyForTest()),
              IsOkAndHolds(MakeMainKeyForTest()));
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()),
              IsOkAndHolds(MakeMainKeyForTest()));
}

TEST(EncryptedUssCorruptionTest, NoEncryptionAlgorithm) {
  // Clear the encryption algorithm.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.encryption_algorithm.reset();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, UnknownEncryptionAlgorithm) {
  // Set the encryption algorithm to max, an unknown value.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container
      .encryption_algorithm = static_cast<UserSecretStashEncryptionAlgorithm>(
      std::numeric_limits<
          std::underlying_type_t<UserSecretStashEncryptionAlgorithm>>::max());
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoCiphertext) {
  // Blank out the ciphertext.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.ciphertext.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, BadCiphertext) {
  // Corrupt the ciphertext.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.ciphertext);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can parse the container but not decrypt it.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->DecryptPayload(MakeMainKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoIv) {
  // Blank the IV.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.iv.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, CorruptIv) {
  // Corrupt the IV.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.iv);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can parse the container but not decrypt it.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->DecryptPayload(MakeMainKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, ShortIv) {
  // Truncate the IV to half its size.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.iv.resize(fb_container.iv.size() / 2);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoGcmTag) {
  // Blank the GCM tag.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.gcm_tag.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, CorruptGcmTag) {
  // Corrupt the GCM tag.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.gcm_tag);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can parse the container but not decrypt it.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->DecryptPayload(MakeMainKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, ShortGcmTag) {
  // Truncate the GCM tag to half its size.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.gcm_tag.resize(fb_container.gcm_tag.size() / 2);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't parse the container.
  EXPECT_THAT(EncryptedUss::FromBlob(*flatbuffer), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoWrappingId) {
  // Change the password wrapping ID to blank. We shouldn't be able to use it.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[0].wrapping_id.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the main key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("", MakePasswordKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, ExtraBlankWrappingIdIsIgnored) {
  // Add a copy of the password wrapped key with a blank ID. It should have no
  // impact on the existing wrapped key.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks.push_back(fb_container.wrapped_key_blocks[0]);
  fb_container.wrapped_key_blocks.back().wrapping_id.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // The password key should work.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("password", MakePasswordKeyForTest()),
              IsOkAndHolds(MakeMainKeyForTest()));
}

TEST(EncryptedUssCorruptionTest, ExtraDuplicateWrappingIdsAreIgnored) {
  // Add a copy of the PIN wrapped key with the same ID as password. It should
  // be ignored and only the first copy used.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks.push_back(fb_container.wrapped_key_blocks[1]);
  fb_container.wrapped_key_blocks.back().wrapping_id = "password";
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // The password key should work.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("password", MakePasswordKeyForTest()),
              IsOkAndHolds(MakeMainKeyForTest()));
}

TEST(EncryptedUssCorruptionTest, NoWrappedKeyEncryptionAlgorithm) {
  // Clear the PIN encryption algorithm.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].encryption_algorithm.reset();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, UnknownWrappedKeyEncryptionAlgorithm) {
  // Set the PIN encryption algorithm to max, an unknown value.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1]
      .encryption_algorithm = static_cast<UserSecretStashEncryptionAlgorithm>(
      std::numeric_limits<
          std::underlying_type_t<UserSecretStashEncryptionAlgorithm>>::max());
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoWrappedKeyBody) {
  // Blank out the encrypted main key.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].encrypted_key.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, BadWrappedKeyBody) {
  // Corrupt the encrypted main key.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.wrapped_key_blocks[1].encrypted_key);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoWrappedKeyIv) {
  // Blank the PIN IV.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].iv.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, CorruptWrappedKeyIv) {
  // Corrupt the PIN IV.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.wrapped_key_blocks[1].iv);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, ShortWrappedKeyIv) {
  // Truncate the PIN IV to half its size.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].iv.resize(
      fb_container.wrapped_key_blocks[1].iv.size() / 2);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, NoWrappedKeyGcmTag) {
  // Blank the PIN GCM tag.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].gcm_tag.clear();
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, CorruptWrappedKeyGcmTag) {
  // Corrupt the PIN GCM tag.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  CorruptBlob(fb_container.wrapped_key_blocks[1].gcm_tag);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

TEST(EncryptedUssCorruptionTest, ShortWrappedKeyGcmTag) {
  // Truncate the PIN GCM tag to half its size.
  UserSecretStashContainer fb_container = MakeFlatbufferForTest();
  fb_container.wrapped_key_blocks[1].gcm_tag.resize(
      fb_container.wrapped_key_blocks[1].gcm_tag.size() / 2);
  auto flatbuffer = fb_container.Serialize();
  ASSERT_THAT(flatbuffer, Optional(_));

  // We can't unwrap the PIN key.
  auto uss = EncryptedUss::FromBlob(*flatbuffer);
  ASSERT_THAT(uss, IsOk());
  EXPECT_THAT(uss->UnwrapMainKey("pin", MakePinKeyForTest()), NotOk());
}

}  // namespace
}  // namespace cryptohome
