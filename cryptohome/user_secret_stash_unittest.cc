// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/optional.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

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

}  // namespace

TEST(UserSecretStashTest, InitializeRandomTest) {
  UserSecretStash stash;
  stash.InitializeRandom();
  EXPECT_TRUE(stash.HasFileSystemKey());
  EXPECT_FALSE(stash.GetFileSystemKey().empty());
  EXPECT_TRUE(stash.HasResetSecret());
  EXPECT_FALSE(stash.GetResetSecret().empty());
}

TEST(UserSecretStashTest, GetEncryptedUSS) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  // No raw secrets in the encrypted USS, which is written to disk.
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash.GetFileSystemKey()));
  EXPECT_FALSE(FindBlobInBlob(*uss_container, stash.GetResetSecret()));
}

TEST(UserSecretStashTest, EncryptAndDecryptUSS) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  UserSecretStash stash2;
  ASSERT_TRUE(stash2.FromEncryptedContainer(uss_container.value(), main_key));

  EXPECT_EQ(stash.GetFileSystemKey(), stash2.GetFileSystemKey());
  EXPECT_EQ(stash.GetResetSecret(), stash2.GetResetSecret());
}

// Test that deserialization fails on an empty blob.
TEST(UserSecretStashTest, DecryptErrorEmptyBuf) {
  brillo::SecureBlob main_key(kAesGcm256KeySize);
  UserSecretStash stash;
  EXPECT_FALSE(stash.FromEncryptedContainer(brillo::SecureBlob(), main_key));
}

// Test that deserialization fails on a corrupted flatbuffer.
TEST(UserSecretStashTest, DecryptErrorCorruptedBuf) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  brillo::SecureBlob corrupted_uss_container = *uss_container;
  for (uint8_t& byte : corrupted_uss_container)
    byte ^= 1;

  EXPECT_FALSE(stash.FromEncryptedContainer(corrupted_uss_container, main_key));
}

// Test that decryption fails on an empty decryption key.
TEST(UserSecretStashTest, DecryptErrorEmptyKey) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  EXPECT_FALSE(stash.FromEncryptedContainer(*uss_container, /*main_key=*/{}));
}

// Test that decryption fails on a decryption key of a wrong size.
TEST(UserSecretStashTest, DecryptErrorKeyBadSize) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  brillo::SecureBlob bad_size_main_key = main_key;
  bad_size_main_key.resize(kAesGcm256KeySize - 1);
  EXPECT_FALSE(stash.FromEncryptedContainer(*uss_container, bad_size_main_key));
}

// Test that decryption fails on a wrong decryption key.
TEST(UserSecretStashTest, DecryptErrorWrongKey) {
  UserSecretStash stash;
  stash.InitializeRandom();

  brillo::SecureBlob main_key(kAesGcm256KeySize);
  memset(main_key.data(), 0xA, main_key.size());

  base::Optional<brillo::SecureBlob> uss_container =
      stash.GetEncryptedContainer(main_key);
  ASSERT_NE(base::nullopt, uss_container);

  brillo::SecureBlob wrong_main_key = main_key;
  wrong_main_key[0] ^= 1;

  EXPECT_FALSE(stash.FromEncryptedContainer(*uss_container, wrong_main_key));
}

// Fixture that helps to read/manipulate the USS flatbuffer's internals using
// FlatBuffers Object API.
class UserSecretStashObjectApiTest : public ::testing::Test {
 protected:
  UserSecretStashObjectApiTest() {
    stash_.InitializeRandom();
    main_key_.resize(kAesGcm256KeySize);
    memset(main_key_.data(), 0xA, main_key_.size());
  }

  void SetUp() override {
    // Encrypt the USS.
    base::Optional<brillo::SecureBlob> uss_container =
        stash_.GetEncryptedContainer(main_key_);
    ASSERT_TRUE(uss_container);

    // Unpack the wrapped USS flatbuffer to |uss_container_obj_|.
    std::unique_ptr<UserSecretStashContainerT> uss_container_obj_ptr =
        UnPackUserSecretStashContainer(uss_container->data());
    ASSERT_TRUE(uss_container_obj_ptr);
    uss_container_obj_ = *uss_container_obj_ptr;

    // Decrypt and unpack the USS flatbuffer to |uss_payload_obj_|.
    brillo::SecureBlob uss_payload;
    ASSERT_TRUE(AesGcmDecrypt(
        brillo::SecureBlob(uss_container_obj_.ciphertext),
        /*ad=*/base::nullopt,
        brillo::SecureBlob(uss_container_obj_.aes_gcm_tag), main_key_,
        brillo::SecureBlob(uss_container_obj_.iv), &uss_payload));
    std::unique_ptr<UserSecretStashPayloadT> uss_payload_obj_ptr =
        UnPackUserSecretStashPayload(uss_payload.data());
    ASSERT_TRUE(uss_payload_obj_ptr);
    uss_payload_obj_ = *uss_payload_obj_ptr;
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
    EXPECT_TRUE(AesGcmEncrypt(uss_payload, /*ad=*/base::nullopt, main_key_, &iv,
                              &tag, &ciphertext));

    // Create a copy of |uss_container_obj_|, with the encrypted blob replaced.
    UserSecretStashContainerT new_uss_container_obj = uss_container_obj_;
    new_uss_container_obj.ciphertext.assign(ciphertext.begin(),
                                            ciphertext.end());
    new_uss_container_obj.iv.assign(iv.begin(), iv.end());
    new_uss_container_obj.aes_gcm_tag.assign(tag.begin(), tag.end());

    // Pack |new_uss_container_obj|.
    builder.Finish(
        UserSecretStashContainer::Pack(builder, &new_uss_container_obj));
    return brillo::SecureBlob(builder.GetBufferPointer(),
                              builder.GetBufferPointer() + builder.GetSize());
  }

  UserSecretStash stash_;
  brillo::SecureBlob main_key_;
  UserSecretStashContainerT uss_container_obj_;
  UserSecretStashPayloadT uss_payload_obj_;
};

// Test that decryption fails when the encryption algorithm is not set.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoAlgorithm) {
  uss_container_obj_.encryption_algorithm =
      UserSecretStashEncryptionAlgorithm::NONE;

  UserSecretStash stash2;
  EXPECT_FALSE(stash2.FromEncryptedContainer(GetFlatbufferFromUssContainerObj(),
                                             main_key_));
}

// Test that decryption fails when the encryption algorithm is unknown.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorUnknownAlgorithm) {
  // It's OK to increment MAX and get an unknown enum, since the schema defines
  // the enum's underlying type to be a 32-bit int.
  uss_container_obj_.encryption_algorithm =
      static_cast<UserSecretStashEncryptionAlgorithm>(
          static_cast<int>(UserSecretStashEncryptionAlgorithm::MAX) + 1);

  UserSecretStash stash2;
  EXPECT_FALSE(stash2.FromEncryptedContainer(GetFlatbufferFromUssContainerObj(),
                                             main_key_));
}

// Test that decryption fails when the ciphertext field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoCiphertext) {
  uss_container_obj_.ciphertext.clear();

  UserSecretStash stash2;
  EXPECT_FALSE(stash2.FromEncryptedContainer(GetFlatbufferFromUssContainerObj(),
                                             main_key_));
}

// Test that decryption fails when the iv field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoIv) {
  uss_container_obj_.iv.clear();

  UserSecretStash stash2;
  EXPECT_FALSE(stash2.FromEncryptedContainer(GetFlatbufferFromUssContainerObj(),
                                             main_key_));
}

// Test that decryption fails when the aes_gcm_tag field is missing.
TEST_F(UserSecretStashObjectApiTest, DecryptErrorNoAesGcmTag) {
  uss_container_obj_.aes_gcm_tag.clear();

  UserSecretStash stash2;
  EXPECT_FALSE(stash2.FromEncryptedContainer(GetFlatbufferFromUssContainerObj(),
                                             main_key_));
}

// Test the decryption succeeds when the payload's file_system_key field is
// missing.
TEST_F(UserSecretStashObjectApiTest, DecryptWithoutFileSystemKey) {
  uss_payload_obj_.file_system_key.clear();

  UserSecretStash stash2;
  ASSERT_TRUE(stash2.FromEncryptedContainer(GetFlatbufferFromUssPayloadObj(),
                                            main_key_));
  EXPECT_FALSE(stash2.HasFileSystemKey());
  EXPECT_EQ(stash_.GetResetSecret(), stash2.GetResetSecret());
}

// Test the decryption succeeds when the payload's reset_secret field is
// missing.
TEST_F(UserSecretStashObjectApiTest, DecryptWithoutResetSecret) {
  uss_payload_obj_.reset_secret.clear();

  UserSecretStash stash2;
  ASSERT_TRUE(stash2.FromEncryptedContainer(GetFlatbufferFromUssPayloadObj(),
                                            main_key_));
  EXPECT_EQ(stash_.GetFileSystemKey(), stash2.GetFileSystemKey());
  EXPECT_FALSE(stash2.HasResetSecret());
}

}  // namespace cryptohome
