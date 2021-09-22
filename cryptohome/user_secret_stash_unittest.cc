// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/user_secret_stash_container_generated.h"
#include "cryptohome/user_secret_stash_payload_generated.h"

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
  base::Optional<brillo::SecureBlob> got_main_key1 =
      stash_->UnwrapMainKey(kWrappingId1, kWrappingKey1);
  ASSERT_TRUE(got_main_key1);
  EXPECT_EQ(*got_main_key1, kMainKey);
  base::Optional<brillo::SecureBlob> got_main_key2 =
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
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

  // No raw secrets in the encrypted USS, which is written to disk.
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash_->GetFileSystemKey()));
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash_->GetResetSecret()));
}

TEST_F(UserSecretStashTest, EncryptAndDecryptUSS) {
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

  std::unique_ptr<UserSecretStash> stash2 =
      UserSecretStash::FromEncryptedContainer(uss_container.value(), kMainKey);
  ASSERT_TRUE(stash2);

  EXPECT_EQ(stash_->GetFileSystemKey(), stash2->GetFileSystemKey());
  EXPECT_EQ(stash_->GetResetSecret(), stash2->GetResetSecret());
}

// Test that deserialization fails on an empty blob.
TEST_F(UserSecretStashTest, DecryptErrorEmptyBuf) {
  EXPECT_FALSE(
      UserSecretStash::FromEncryptedContainer(brillo::SecureBlob(), kMainKey));
}

// Test that deserialization fails on a corrupted flatbuffer.
TEST_F(UserSecretStashTest, DecryptErrorCorruptedBuf) {
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

  brillo::SecureBlob corrupted_uss_container = *uss_container;
  for (uint8_t& byte : corrupted_uss_container)
    byte ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(corrupted_uss_container,
                                                       kMainKey));
}

// Test that decryption fails on an empty decryption key.
TEST_F(UserSecretStashTest, DecryptErrorEmptyKey) {
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

  EXPECT_FALSE(
      UserSecretStash::FromEncryptedContainer(*uss_container, /*main_key=*/{}));
}

// Test that decryption fails on a decryption key of a wrong size.
TEST_F(UserSecretStashTest, DecryptErrorKeyBadSize) {
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

  brillo::SecureBlob bad_size_main_key = kMainKey;
  bad_size_main_key.resize(kAesGcm256KeySize - 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(*uss_container,
                                                       bad_size_main_key));
}

// Test that decryption fails on a wrong decryption key.
TEST_F(UserSecretStashTest, DecryptErrorWrongKey) {
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

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
  ASSERT_NE(base::nullopt, uss_container);
  std::unique_ptr<UserSecretStash> stash2 =
      UserSecretStash::FromEncryptedContainer(uss_container.value(), kMainKey);
  ASSERT_TRUE(stash2);

  // The wrapped key blocks are present in the loaded stash and can be
  // decrypted.
  EXPECT_TRUE(stash2->HasWrappedMainKey(kWrappingId1));
  EXPECT_TRUE(stash2->HasWrappedMainKey(kWrappingId2));
  base::Optional<brillo::SecureBlob> got_main_key1 =
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
  base::Optional<brillo::SecureBlob> uss_container =
      stash_->GetEncryptedContainer(kMainKey);
  ASSERT_NE(base::nullopt, uss_container);

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

// Fixture that helps to read/manipulate the USS flatbuffer's internals using
// FlatBuffers Object API.
class UserSecretStashObjectApiTest : public UserSecretStashTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(UserSecretStashTest::SetUp());

    // Encrypt the USS.
    base::Optional<brillo::SecureBlob> uss_container =
        stash_->GetEncryptedContainer(kMainKey);
    ASSERT_TRUE(uss_container);

    // Unpack the wrapped USS flatbuffer to |uss_container_obj_|.
    std::unique_ptr<UserSecretStashContainerT> uss_container_obj_ptr =
        UnPackUserSecretStashContainer(uss_container->data());
    ASSERT_TRUE(uss_container_obj_ptr);
    uss_container_obj_ = std::move(*uss_container_obj_ptr);

    // Decrypt and unpack the USS flatbuffer to |uss_payload_obj_|.
    brillo::SecureBlob uss_payload;
    ASSERT_TRUE(AesGcmDecrypt(
        brillo::SecureBlob(uss_container_obj_.ciphertext),
        /*ad=*/base::nullopt, brillo::SecureBlob(uss_container_obj_.gcm_tag),
        kMainKey, brillo::SecureBlob(uss_container_obj_.iv), &uss_payload));
    std::unique_ptr<UserSecretStashPayloadT> uss_payload_obj_ptr =
        UnPackUserSecretStashPayload(uss_payload.data());
    ASSERT_TRUE(uss_payload_obj_ptr);
    uss_payload_obj_ = std::move(*uss_payload_obj_ptr);
  }

  brillo::SecureBlob GetFlatbufferFromUssContainerObj() const {
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(
        UserSecretStashContainer::Pack(builder, &uss_container_obj_));
    return brillo::SecureBlob(builder.GetBufferPointer(),
                              builder.GetBufferPointer() + builder.GetSize());
  }

  brillo::SecureBlob GetFlatbufferFromUssPayloadObj() const {
    // Pack |uss_payload_obj_|.
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(UserSecretStashPayload::Pack(builder, &uss_payload_obj_));
    brillo::SecureBlob uss_payload(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
    builder.Clear();

    // Encrypt the packed |uss_payload_obj_|.
    brillo::SecureBlob iv, tag, ciphertext;
    EXPECT_TRUE(AesGcmEncrypt(uss_payload, /*ad=*/base::nullopt, kMainKey, &iv,
                              &tag, &ciphertext));

    // Create a copy of |uss_container_obj_|, with the encrypted blob replaced.
    UserSecretStashContainerT new_uss_container_obj;
    new_uss_container_obj.encryption_algorithm =
        uss_container_obj_.encryption_algorithm;
    new_uss_container_obj.ciphertext.assign(ciphertext.begin(),
                                            ciphertext.end());
    new_uss_container_obj.iv.assign(iv.begin(), iv.end());
    new_uss_container_obj.gcm_tag.assign(tag.begin(), tag.end());
    // Need to clone the nested tables manually, as Flatbuffers don't provide a
    // copy constructor.
    for (const std::unique_ptr<UserSecretStashWrappedKeyBlockT>& key_block :
         uss_container_obj_.wrapped_key_blocks) {
      new_uss_container_obj.wrapped_key_blocks.push_back(
          std::make_unique<UserSecretStashWrappedKeyBlockT>(*key_block));
    }

    // Pack |new_uss_container_obj|.
    builder.Finish(
        UserSecretStashContainer::Pack(builder, &new_uss_container_obj));
    return brillo::SecureBlob(builder.GetBufferPointer(),
                              builder.GetBufferPointer() + builder.GetSize());
  }

  UserSecretStashContainerT uss_container_obj_;
  UserSecretStashPayloadT uss_payload_obj_;
};

// Verify that the test fixture correctly generates the flatbuffers from the
// Object API.
TEST_F(UserSecretStashObjectApiTest, SmokeTest) {
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadObj(), kMainKey));
  EXPECT_TRUE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the encryption algorithm is not set.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoAlgorithm) {
  uss_container_obj_.encryption_algorithm =
      UserSecretStashEncryptionAlgorithm::NONE;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the encryption algorithm is unknown.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorUnknownAlgorithm) {
  // It's OK to increment MAX and get an unknown enum, since the schema defines
  // the enum's underlying type to be a 32-bit int.
  uss_container_obj_.encryption_algorithm =
      static_cast<UserSecretStashEncryptionAlgorithm>(
          static_cast<int>(UserSecretStashEncryptionAlgorithm::MAX) + 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the ciphertext field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoCiphertext) {
  uss_container_obj_.ciphertext.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the ciphertext field is corrupted.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorCorruptedCiphertext) {
  for (uint8_t& byte : uss_container_obj_.ciphertext)
    byte ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the iv field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoIv) {
  uss_container_obj_.iv.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the iv field has a wrong value.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorWrongIv) {
  uss_container_obj_.iv[0] ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the iv field is of a wrong size.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorIvBadSize) {
  uss_container_obj_.iv.resize(uss_container_obj_.iv.size() - 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the gcm_tag field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoGcmTag) {
  uss_container_obj_.gcm_tag.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the gcm_tag field has a wrong value.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorWrongGcmTag) {
  uss_container_obj_.gcm_tag[0] ^= 1;

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test that decryption fails when the gcm_tag field is of a wrong size.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorGcmTagBadSize) {
  uss_container_obj_.gcm_tag.resize(uss_container_obj_.gcm_tag.size() - 1);

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssContainerObj(), kMainKey));
}

// Test the decryption fails when the payload's file_system_key field is
// missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoFileSystemKey) {
  uss_payload_obj_.file_system_key.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadObj(), kMainKey));
}

// Test the decryption fails when the payload's reset_secret field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoResetSecret) {
  uss_payload_obj_.reset_secret.clear();

  EXPECT_FALSE(UserSecretStash::FromEncryptedContainer(
      GetFlatbufferFromUssPayloadObj(), kMainKey));
}

}  // namespace cryptohome
