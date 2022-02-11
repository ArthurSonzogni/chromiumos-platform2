// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"

namespace cryptohome {

namespace {

static bool FindBlobInBlob(const brillo::SecureBlob& haystack,
                           const brillo::SecureBlob& needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(),
                     needle.end()) != haystack.end();
}

class UserSecretStashTest : public ::testing::Test {
 protected:
  const brillo::SecureBlob kMainKey =
      brillo::SecureBlob(kAesGcm256KeySize, 0xA);

  void SetUp() override {
    stash_ = UserSecretStash::CreateRandom();
    ASSERT_TRUE(stash_);
  }

  std::unique_ptr<UserSecretStash> stash_;
};

}  // namespace

TEST_F(UserSecretStashTest, CreateRandom) {
  EXPECT_FALSE(stash_->GetFileSystemKey().empty());
  EXPECT_FALSE(stash_->GetResetSecret().empty());
  // The secrets should be created randomly and never collide (in practice).
  EXPECT_NE(stash_->GetFileSystemKey(), stash_->GetResetSecret());
}

// Verify that the USS secrets created by CreateRandom() don't repeat (in
// practice).
TEST_F(UserSecretStashTest, CreateRandomNotConstant) {
  std::unique_ptr<UserSecretStash> stash2 = UserSecretStash::CreateRandom();
  ASSERT_TRUE(stash2);
  EXPECT_NE(stash_->GetFileSystemKey(), stash2->GetFileSystemKey());
  EXPECT_NE(stash_->GetResetSecret(), stash2->GetResetSecret());
}

// Basic test of the `CreateRandomMainKey()` method.
TEST_F(UserSecretStashTest, CreateRandomMainKey) {
  brillo::SecureBlob main_key = UserSecretStash::CreateRandomMainKey();
  EXPECT_FALSE(main_key.empty());
}

// Test the secret main keys created by `CreateRandomMainKey()` don't repeat (in
// practice).
TEST_F(UserSecretStashTest, CreateRandomMainKeyNotConstant) {
  brillo::SecureBlob main_key_1 = UserSecretStash::CreateRandomMainKey();
  brillo::SecureBlob main_key_2 = UserSecretStash::CreateRandomMainKey();
  EXPECT_NE(main_key_1, main_key_2);
}

// Verify the getters/setters of the wrapped key fields.
TEST_F(UserSecretStashTest, MainKeyWrapping) {
  const char kWrappingId1[] = "id1";
  const char kWrappingId2[] = "id2";
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xB);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xC);

  // Initially there's no wrapped key.
  EXPECT_FALSE(stash_->HasWrappedMainKey(kWrappingId1));
  EXPECT_FALSE(stash_->HasWrappedMainKey(kWrappingId2));

  // And the main key wrapped with two wrapping keys.
  EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId1, kWrappingKey1));
  EXPECT_TRUE(stash_->HasWrappedMainKey(kWrappingId1));
  EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId2, kWrappingKey2));
  EXPECT_TRUE(stash_->HasWrappedMainKey(kWrappingId2));
  // Duplicate wrapping IDs aren't allowed.
  EXPECT_FALSE(
      stash_->AddWrappedMainKey(kMainKey, kWrappingId1, kWrappingKey1));

  // The main key can be unwrapped using any of the wrapping keys.
  std::optional<brillo::SecureBlob> got_main_key1 =
      stash_->UnwrapMainKey(kWrappingId1, kWrappingKey1);
  ASSERT_TRUE(got_main_key1);
  EXPECT_EQ(*got_main_key1, kMainKey);
  std::optional<brillo::SecureBlob> got_main_key2 =
      stash_->UnwrapMainKey(kWrappingId2, kWrappingKey2);
  ASSERT_TRUE(got_main_key2);
  EXPECT_EQ(*got_main_key2, kMainKey);

  // Removal of one wrapped key block preserves the other.
  EXPECT_TRUE(stash_->RemoveWrappedMainKey(kWrappingId1));
  EXPECT_FALSE(stash_->HasWrappedMainKey(kWrappingId1));
  EXPECT_TRUE(stash_->HasWrappedMainKey(kWrappingId2));
  // Removing a non-existing wrapped key block fails.
  EXPECT_FALSE(stash_->RemoveWrappedMainKey(kWrappingId1));
}

TEST_F(UserSecretStashTest, GetEncryptedUSS) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  // No raw secrets in the encrypted USS, which is written to disk.
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash_->GetFileSystemKey()));
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash_->GetResetSecret()));
}

TEST_F(UserSecretStashTest, EncryptAndDecryptUSS) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  std::unique_ptr<UserSecretStash> stash2 =
      UserSecretStash::FromEncryptedContainer(uss_container.value(), kMainKey);
  ASSERT_TRUE(stash2);

  EXPECT_EQ(stash_->GetFileSystemKey(), stash2->GetFileSystemKey());
  EXPECT_EQ(stash_->GetResetSecret(), stash2->GetResetSecret());
}

// Test that deserialization fails on an empty blob. Normally this never occurs,
// but we verify to be resilient against accidental or intentional file
// corruption.
TEST_F(UserSecretStashTest, DecryptErrorEmptyBuf) {
  EXPECT_FALSE(
      UserSecretStash::FromEncryptedContainer(brillo::SecureBlob(), kMainKey));
}

// Test that deserialization fails on a corrupted flatbuffer. Normally this
// never occurs, but we verify to be resilient against accidental or intentional
// file corruption.
TEST_F(UserSecretStashTest, DecryptErrorCorruptedBuf) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  brillo::SecureBlob corrupted_uss_container = *uss_container;
  for (uint8_t& byte : corrupted_uss_container)
    byte ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(corrupted_uss_container,
                                                       kMainKey));
}

// Test that decryption fails on an empty decryption key.
TEST_F(UserSecretStashTest, DecryptErrorEmptyKey) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  EXPECT_FALSE(
      UserSecretStash::FromEncryptedContainer(*uss_container, /*main_key=*/{}));
}

// Test that decryption fails on a decryption key of a wrong size.
TEST_F(UserSecretStashTest, DecryptErrorKeyBadSize) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  brillo::SecureBlob bad_size_main_key = kMainKey;
  bad_size_main_key.resize(kAesGcm256KeySize - 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(*uss_container,
                                                       bad_size_main_key));
}

// Test that decryption fails on a wrong decryption key.
TEST_F(UserSecretStashTest, DecryptErrorWrongKey) {
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  brillo::SecureBlob wrong_main_key = kMainKey;
  wrong_main_key[0] ^= 1;

  EXPECT_FALSE(
      UserSecretStash::FromEncryptedContainer(*uss_container, wrong_main_key));
}

// Test that wrapped key blocks are [de]serialized correctly.
TEST_F(UserSecretStashTest, EncryptAndDecryptUSSWithWrappedKeys) {
  const char kWrappingId1[] = "id1";
  const char kWrappingId2[] = "id2";
  const brillo::SecureBlob kWrappingKey1(kAesGcm256KeySize, 0xB);
  const brillo::SecureBlob kWrappingKey2(kAesGcm256KeySize, 0xC);

  // Add wrapped key blocks.
  EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId1, kWrappingKey1));
  EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId2, kWrappingKey2));

  // Do the serialization-deserialization roundtrip with the USS.
  auto uss_container = stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);
  std::unique_ptr<UserSecretStash> stash2 =
      UserSecretStash::FromEncryptedContainer(uss_container.value(), kMainKey);
  ASSERT_TRUE(stash2);

  // The wrapped key blocks are present in the loaded stash and can be
  // decrypted.
  EXPECT_TRUE(stash2->HasWrappedMainKey(kWrappingId1));
  EXPECT_TRUE(stash2->HasWrappedMainKey(kWrappingId2));
  std::optional<brillo::SecureBlob> got_main_key1 =
      stash2->UnwrapMainKey(kWrappingId1, kWrappingKey1);
  ASSERT_TRUE(got_main_key1);
  EXPECT_EQ(*got_main_key1, kMainKey);
}

// Test that the USS can be loaded and decrypted using the wrapping key stored
// in it.
TEST_F(UserSecretStashTest, EncryptAndDecryptUSSViaWrappedKey) {
  // Add a wrapped key block.
  const char kWrappingId[] = "id";
  const brillo::SecureBlob kWrappingKey(kAesGcm256KeySize, 0xB);
  EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId, kWrappingKey));

  // Encrypt the USS.
  std::optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(std::nullopt, uss_container);

  // The USS can be decrypted using the wrapping key.
  brillo::SecureBlob unwrapped_main_key;
  std::unique_ptr<UserSecretStash> stash2 =
      UserSecretStash::FromEncryptedContainerWithWrappingKey(
          uss_container.value(), kWrappingId, kWrappingKey,
          &unwrapped_main_key);
  ASSERT_TRUE(stash2);
  EXPECT_EQ(stash_->GetFileSystemKey(), stash2->GetFileSystemKey());
  EXPECT_EQ(stash_->GetResetSecret(), stash2->GetResetSecret());
  EXPECT_EQ(unwrapped_main_key, kMainKey);
}

// Test the USS experiment state is off by default, but can be toggled in tests.
TEST_F(UserSecretStashTest, ExperimentState) {
  // The experiment is off by default.
  EXPECT_FALSE(IsUserSecretStashExperimentEnabled());

  // Verify the test can toggle the experiment state.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  EXPECT_TRUE(IsUserSecretStashExperimentEnabled());

  // Unset the experiment override to avoid affecting other test cases.
  SetUserSecretStashExperimentForTesting(/*enabled=*/std::nullopt);
}

// Fixture that helps to read/manipulate the USS flatbuffer's internals using
// the flatbuffer C++ bindings.
class UserSecretStashInternalsTest : public UserSecretStashTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(UserSecretStashTest::SetUp());
    ASSERT_NO_FATAL_FAILURE(UpdateBindingStrusts());
  }

  // Populates |uss_container_struct_| and |uss_payload_struct_| based on
  // |stash_|.
  void UpdateBindingStrusts() {
    // Encrypt the USS.
    std::optional<brillo::SecureBlob> uss_container =
        stash_->GetEncryptedContainer(kMainKey);
    ASSERT_TRUE(uss_container);

    // Unpack the wrapped USS flatbuffer to |uss_container_struct_|.
    std::optional<UserSecretStashContainer> uss_container_struct =
        UserSecretStashContainer::Deserialize(uss_container.value());
    ASSERT_TRUE(uss_container_struct);
    uss_container_struct_ = std::move(uss_container_struct.value());

    // Decrypt and unpack the USS flatbuffer to |uss_payload_struct_|.
    brillo::SecureBlob uss_payload;
    ASSERT_TRUE(AesGcmDecrypt(
        brillo::SecureBlob(uss_container_struct_.ciphertext),
        /*ad=*/std::nullopt, brillo::SecureBlob(uss_container_struct_.gcm_tag),
        kMainKey, brillo::SecureBlob(uss_container_struct_.iv), &uss_payload));
    std::optional<UserSecretStashPayload> uss_payload_struct =
        UserSecretStashPayload::Deserialize(uss_payload);
    ASSERT_TRUE(uss_payload_struct);
    uss_payload_struct_ = std::move(*uss_payload_struct);
  }

  // Converts |uss_container_struct_| => "container flatbuffer".
  brillo::SecureBlob GetFlatbufferFromUssContainerStruct() const {
    std::optional<brillo::SecureBlob> serialized =
        uss_container_struct_.Serialize();
    if (!serialized.has_value()) {
      ADD_FAILURE() << "Failed to serialized UserSecretStashContainer";
      return brillo::SecureBlob();
    }
    return serialized.value();
  }

  // Converts |uss_payload_struct_| => "payload flatbuffer" =>
  // UserSecretStashContainer => "container flatbuffer".
  brillo::SecureBlob GetFlatbufferFromUssPayloadStruct() const {
    return GetFlatbufferFromUssPayloadBlob(PackUssPayloadStruct());
  }

  // Converts |uss_payload_struct_| => "payload flatbuffer".
  brillo::SecureBlob PackUssPayloadStruct() const {
    std::optional<brillo::SecureBlob> serialized =
        uss_payload_struct_.Serialize();
    if (!serialized.has_value()) {
      ADD_FAILURE() << "Failed to serialized UserSecretStashPayload";
      return brillo::SecureBlob();
    }
    return serialized.value();
  }

  // Converts "payload flatbuffer" => UserSecretStashContainer => "container
  // flatbuffer".
  brillo::SecureBlob GetFlatbufferFromUssPayloadBlob(
      const brillo::SecureBlob& uss_payload) const {
    // Encrypt the packed |uss_payload_struct_|.
    brillo::SecureBlob iv, tag, ciphertext;
    EXPECT_TRUE(AesGcmEncrypt(uss_payload, /*ad=*/std::nullopt, kMainKey, &iv,
                              &tag, &ciphertext));

    // Create a copy of |uss_container_struct_|, with the encrypted blob
    // replaced.
    UserSecretStashContainer new_uss_container_struct = uss_container_struct_;
    new_uss_container_struct.ciphertext = ciphertext;
    new_uss_container_struct.iv = iv;
    new_uss_container_struct.gcm_tag = tag;

    // Pack |new_uss_container_struct|.
    std::optional<brillo::SecureBlob> serialized =
        new_uss_container_struct.Serialize();
    if (!serialized.has_value()) {
      ADD_FAILURE() << "Failed to seralize the USS container";
      return brillo::SecureBlob();
    }
    return serialized.value();
  }

  UserSecretStashContainer uss_container_struct_;
  UserSecretStashPayload uss_payload_struct_;
};

// Verify that the test fixture correctly generates the USS flatbuffers from the
// binding structs.
TEST_F(UserSecretStashInternalsTest, SmokeTest) {
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadBlob(PackUssPayloadStruct()), kMainKey));
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadStruct(), kMainKey));
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the USS payload is a corrupted flatbuffer.
// Normally this never occurs, but we verify to be resilient against accidental
// or intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorBadPayload) {
  brillo::SecureBlob uss_payload = PackUssPayloadStruct();
  for (uint8_t& byte : uss_payload)
    byte ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadBlob(uss_payload), kMainKey));
}

// Test that decryption fails when the USS payload is a truncated flatbuffer.
// Normally this never occurs, but we verify to be resilient against accidental
// or intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorPayloadBadSize) {
  brillo::SecureBlob uss_payload = PackUssPayloadStruct();
  uss_payload.resize(uss_payload.size() / 2);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadBlob(uss_payload), kMainKey));
}

// Test that decryption fails when the encryption algorithm is not set. Normally
// this never occurs, but we verify to be resilient against accidental or
// intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoAlgorithm) {
  uss_container_struct_.encryption_algorithm.reset();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the encryption algorithm is unknown. Normally
// this never occurs, but we verify to be resilient against accidental or
// intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorUnknownAlgorithm) {
  uss_container_struct_
      .encryption_algorithm = static_cast<UserSecretStashEncryptionAlgorithm>(
      std::numeric_limits<
          std::underlying_type_t<UserSecretStashEncryptionAlgorithm>>::max());

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the ciphertext field is missing. Normally
// this never occurs, but we verify to be resilient against accidental or
// intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoCiphertext) {
  uss_container_struct_.ciphertext.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the ciphertext field is corrupted. Normally
// this never occurs, but we verify to be resilient against accidental or
// intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorCorruptedCiphertext) {
  for (uint8_t& byte : uss_container_struct_.ciphertext)
    byte ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the iv field is missing. Normally this never
// occurs, but we verify to be resilient against accidental or intentional file
// corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoIv) {
  uss_container_struct_.iv.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the iv field has a wrong value. Normally this
// never occurs, but we verify to be resilient against accidental or intentional
// file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorWrongIv) {
  uss_container_struct_.iv[0] ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the iv field is of a wrong size. Normally
// this never occurs, but we verify to be resilient against accidental or
// intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorIvBadSize) {
  uss_container_struct_.iv.resize(uss_container_struct_.iv.size() - 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the gcm_tag field is missing. Normally this
// never occurs, but we verify to be resilient against accidental or intentional
// file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoGcmTag) {
  uss_container_struct_.gcm_tag.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the gcm_tag field has a wrong value.
TEST_F(UserSecretStashInternalsTest, DecryptErrorWrongGcmTag) {
  uss_container_struct_.gcm_tag[0] ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test that decryption fails when the gcm_tag field is of a wrong size.
// Normally this never occurs, but we verify to be resilient against accidental
// or intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorGcmTagBadSize) {
  uss_container_struct_.gcm_tag.resize(uss_container_struct_.gcm_tag.size() -
                                       1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerStruct(), kMainKey));
}

// Test the decryption fails when the payload's file_system_key field is
// missing. Normally this never occurs, but we verify to be resilient against
// accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoFileSystemKey) {
  uss_payload_struct_.file_system_key.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadStruct(), kMainKey));
}

// Test the decryption fails when the payload's reset_secret field is missing.
// Normally this never occurs, but we verify to be resilient against accidental
// or intentional file corruption.
TEST_F(UserSecretStashInternalsTest, DecryptErrorNoResetSecret) {
  uss_payload_struct_.reset_secret.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadStruct(), kMainKey));
}

// Fixture that prebundles the USS object with a wrapped key block.
class UserSecretStashInternalsWrappingTest
    : public UserSecretStashInternalsTest {
 protected:
  const char* const kWrappingId = "id";
  const brillo::SecureBlob kWrappingKey =
      brillo::SecureBlob(kAesGcm256KeySize, 0xB);

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(UserSecretStashInternalsTest::SetUp());
    EXPECT_TRUE(stash_->AddWrappedMainKey(kMainKey, kWrappingId, kWrappingKey));
    ASSERT_NO_FATAL_FAILURE(UpdateBindingStrusts());
  }
};

// Verify that the test fixture correctly generates the flatbuffers from the
// Object API.
TEST_F(UserSecretStashInternalsWrappingTest, SmokeTest) {
  brillo::SecureBlob main_key;
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
  EXPECT_EQ(main_key, kMainKey);
}

// Test that decryption via wrapping key fails when the only block's wrapping_id
// is empty. Normally this never occurs, but we verify to be resilient against
// accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorNoWrappingId) {
  uss_container_struct_.wrapped_key_blocks[0].wrapping_id = std::string();

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key succeeds despite having an extra block
// with an empty wrapping_id (this block should be ignored). Normally this never
// occurs, but we verify to be resilient against accidental or intentional file
// corruption.
TEST_F(UserSecretStashInternalsWrappingTest, SuccessWithExtraNoWrappingId) {
  UserSecretStashWrappedKeyBlock key_block_clone =
      uss_container_struct_.wrapped_key_blocks[0];
  key_block_clone.wrapping_id = std::string();
  uss_container_struct_.wrapped_key_blocks.push_back(key_block_clone);

  brillo::SecureBlob main_key;
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key succeeds despite having an extra block
// with a duplicate wrapping_id (this block should be ignored). Normally this
// never occurs, but we verify to be resilient against accidental or intentional
// file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, SuccessWithDuplicateWrappingId) {
  UserSecretStashWrappedKeyBlock key_block_clone =
      uss_container_struct_.wrapped_key_blocks[0];
  uss_container_struct_.wrapped_key_blocks.push_back(key_block_clone);

  brillo::SecureBlob main_key;
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the algorithm is not
// specified in the stored block. Normally this never occurs, but we verify to
// be resilient against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorNoAlgorithm) {
  uss_container_struct_.wrapped_key_blocks[0].encryption_algorithm =
      std::nullopt;

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the algorithm is unknown.
// Normally this never occurs, but we verify to be resilient against accidental
// or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorUnknownAlgorithm) {
  uss_container_struct_.wrapped_key_blocks[0]
      .encryption_algorithm = static_cast<UserSecretStashEncryptionAlgorithm>(
      std::numeric_limits<
          std::underlying_type_t<UserSecretStashEncryptionAlgorithm>>::max());

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the encrypted_key is empty
// in the stored block.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorEmptyEncryptedKey) {
  uss_container_struct_.wrapped_key_blocks[0].encrypted_key.clear();

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the encrypted_key in the
// stored block is corrupted.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorBadEncryptedKey) {
  uss_container_struct_.wrapped_key_blocks[0].encrypted_key[0] ^= 1;

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the iv is empty in the
// stored block. Normally this never occurs, but we verify to be resilient
// against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorNoIv) {
  uss_container_struct_.wrapped_key_blocks[0].iv.clear();

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the iv in the stored block
// is corrupted. Normally this never occurs, but we verify to be resilient
// against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorWrongIv) {
  uss_container_struct_.wrapped_key_blocks[0].iv[0] ^= 1;

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the iv in the stored block
// is of wrong size. Normally this never occurs, but we verify to be resilient
// against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorIvBadSize) {
  uss_container_struct_.wrapped_key_blocks[0].iv.resize(
      uss_container_struct_.wrapped_key_blocks[0].iv.size() - 1);

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the gcm_tag is empty in the
// stored block. Normally this never occurs, but we verify to be resilient
// against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorNoGcmTag) {
  uss_container_struct_.wrapped_key_blocks[0].gcm_tag.clear();

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the gcm_tag in the stored
// block is corrupted. Normally this never occurs, but we verify to be resilient
// against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorWrongGcmTag) {
  uss_container_struct_.wrapped_key_blocks[0].gcm_tag[0] ^= 1;

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

// Test that decryption via wrapping key fails when the gcm_tag in the stored
// block is of wrong size. Normally this never occurs, but we verify to be
// resilient against accidental or intentional file corruption.
TEST_F(UserSecretStashInternalsWrappingTest, ErrorGcmTagBadSize) {
  uss_container_struct_.wrapped_key_blocks[0].gcm_tag.resize(
      uss_container_struct_.wrapped_key_blocks[0].gcm_tag.size() - 1);

  brillo::SecureBlob main_key;
  EXPECT_FALSE(UserSecretStash::FromEncryptedContainerWithWrappingKey(
      GetFlatbufferFromUssContainerStruct(), kWrappingId, kWrappingKey,
      &main_key));
}

}  // namespace cryptohome
