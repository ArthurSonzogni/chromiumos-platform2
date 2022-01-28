// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/secure_blob.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"
#include "cryptohome/user_secret_stash_container_generated.h"
#include "cryptohome/user_secret_stash_payload_generated.h"

namespace cryptohome {

namespace {

// Serializes the UserSecretStashWrappedKeyBlock table into the given flatbuffer
// builder. Returns the flatbuffer offset, to be used for building the outer
// table.
flatbuffers::Offset<UserSecretStashWrappedKeyBlock>
GenerateUserSecretStashWrappedKeyBlock(
    const std::string& wrapping_id,
    const UserSecretStash::WrappedKeyBlock& wrapped_key_block,
    flatbuffers::FlatBufferBuilder* builder) {
  // Serialize the table's fields.
  auto wrapping_id_string = builder->CreateString(wrapping_id);
  auto encrypted_key_vector =
      builder->CreateVector(wrapped_key_block.encrypted_key.data(),
                            wrapped_key_block.encrypted_key.size());
  auto iv_vector = builder->CreateVector(wrapped_key_block.iv.data(),
                                         wrapped_key_block.iv.size());
  auto gcm_tag_vector = builder->CreateVector(wrapped_key_block.gcm_tag.data(),
                                              wrapped_key_block.gcm_tag.size());

  // Serialize the table itself.
  UserSecretStashWrappedKeyBlockBuilder table_builder(*builder);
  table_builder.add_wrapping_id(wrapping_id_string);
  table_builder.add_encryption_algorithm(
      wrapped_key_block.encryption_algorithm);
  table_builder.add_encrypted_key(encrypted_key_vector);
  table_builder.add_iv(iv_vector);
  table_builder.add_gcm_tag(gcm_tag_vector);
  return table_builder.Finish();
}

// Serializes the UserSecretStashContainer table. Returns the resulting
// flatbuffer as a blob.
brillo::SecureBlob GenerateUserSecretStashContainer(
    const brillo::SecureBlob& ciphertext,
    const brillo::SecureBlob& tag,
    const brillo::SecureBlob& iv,
    const std::map<std::string, UserSecretStash::WrappedKeyBlock>&
        wrapped_key_blocks) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/4096, &allocator,
                                         /*own_allocator=*/false);

  auto ciphertext_vector =
      builder.CreateVector(ciphertext.data(), ciphertext.size());
  auto tag_vector = builder.CreateVector(tag.data(), tag.size());
  auto iv_vector = builder.CreateVector(iv.data(), iv.size());
  std::vector<flatbuffers::Offset<UserSecretStashWrappedKeyBlock>>
      wrapped_key_block_items;
  for (const auto& item : wrapped_key_blocks) {
    const std::string& wrapping_id = item.first;
    const UserSecretStash::WrappedKeyBlock& wrapped_key_block = item.second;
    wrapped_key_block_items.push_back(GenerateUserSecretStashWrappedKeyBlock(
        wrapping_id, wrapped_key_block, &builder));
  }
  auto wrapped_key_blocks_vector =
      builder.CreateVector(wrapped_key_block_items);

  UserSecretStashContainerBuilder uss_container_builder(builder);
  uss_container_builder.add_encryption_algorithm(
      UserSecretStashEncryptionAlgorithm::AES_GCM_256);
  uss_container_builder.add_ciphertext(ciphertext_vector);
  uss_container_builder.add_gcm_tag(tag_vector);
  uss_container_builder.add_iv(iv_vector);
  uss_container_builder.add_wrapped_key_blocks(wrapped_key_blocks_vector);
  auto uss_container = uss_container_builder.Finish();

  builder.Finish(uss_container);

  auto ret_val =
      brillo::SecureBlob(builder.GetBufferPointer(),
                         builder.GetBufferPointer() + builder.GetSize());

  builder.Clear();

  return ret_val;
}

std::map<std::string, UserSecretStash::WrappedKeyBlock>
LoadUserSecretStashWrappedKeyBlocks(
    const flatbuffers::Vector<
        flatbuffers::Offset<UserSecretStashWrappedKeyBlock>>&
        wrapped_key_block_vector) {
  std::map<std::string, UserSecretStash::WrappedKeyBlock> loaded_key_blocks;

  for (const UserSecretStashWrappedKeyBlock* wrapped_key_block :
       wrapped_key_block_vector) {
    std::string wrapping_id;
    if (wrapped_key_block->wrapping_id()) {
      wrapping_id = wrapped_key_block->wrapping_id()->str();
    }
    if (wrapping_id.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty ID.";
      continue;
    }
    if (loaded_key_blocks.count(wrapping_id)) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with duplicate ID "
          << wrapping_id << ".";
      continue;
    }
    UserSecretStash::WrappedKeyBlock loaded_block;

    if (!wrapped_key_block->encryption_algorithm().has_value()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unset algorithm";
      continue;
    }
    if (wrapped_key_block->encryption_algorithm().value() !=
        UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unknown algorithm: "
                   << static_cast<int>(
                          wrapped_key_block->encryption_algorithm().value());
      continue;
    }
    loaded_block.encryption_algorithm =
        wrapped_key_block->encryption_algorithm().value();

    if (!wrapped_key_block->encrypted_key() ||
        !wrapped_key_block->encrypted_key()->size()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty encrypted key.";
      continue;
    }
    loaded_block.encrypted_key.assign(
        wrapped_key_block->encrypted_key()->begin(),
        wrapped_key_block->encrypted_key()->end());

    if (!wrapped_key_block->iv() || !wrapped_key_block->iv()->size()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty IV.";
      continue;
    }
    loaded_block.iv.assign(wrapped_key_block->iv()->begin(),
                           wrapped_key_block->iv()->end());

    if (!wrapped_key_block->gcm_tag() ||
        !wrapped_key_block->gcm_tag()->size()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty AES-GCM tag.";
      continue;
    }
    loaded_block.gcm_tag.assign(wrapped_key_block->gcm_tag()->begin(),
                                wrapped_key_block->gcm_tag()->end());

    loaded_key_blocks.insert({std::move(wrapping_id), std::move(loaded_block)});
  }

  return loaded_key_blocks;
}

bool LoadUserSecretStashContainer(
    const brillo::SecureBlob& flatbuffer,
    brillo::SecureBlob* ciphertext,
    brillo::SecureBlob* iv,
    brillo::SecureBlob* tag,
    std::map<std::string, UserSecretStash::WrappedKeyBlock>*
        wrapped_key_blocks) {
  flatbuffers::Verifier container_verifier(flatbuffer.data(),
                                           flatbuffer.size());
  if (!VerifyUserSecretStashContainerBuffer(container_verifier)) {
    LOG(ERROR) << "The UserSecretStashContainer flatbuffer is invalid";
    return false;
  }

  auto uss_container = GetUserSecretStashContainer(flatbuffer.data());

  if (!uss_container->encryption_algorithm().has_value()) {
    LOG(ERROR) << "UserSecretStashContainer has no algorithm set";
    return false;
  }
  UserSecretStashEncryptionAlgorithm algorithm =
      uss_container->encryption_algorithm().value();
  if (algorithm != UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStashContainer uses unknown algorithm: "
               << static_cast<int>(algorithm);
    return false;
  }

  if (!uss_container->ciphertext() || !uss_container->ciphertext()->size()) {
    LOG(ERROR) << "UserSecretStash has empty ciphertext";
    return false;
  }
  ciphertext->assign(uss_container->ciphertext()->begin(),
                     uss_container->ciphertext()->end());

  if (!uss_container->iv() || !uss_container->iv()->size()) {
    LOG(ERROR) << "UserSecretStash has empty IV";
    return false;
  }
  if (uss_container->iv()->size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash has IV of wrong length: "
               << uss_container->iv()->size()
               << ", expected: " << kAesGcmIVSize;
    return false;
  }
  iv->assign(uss_container->iv()->begin(), uss_container->iv()->end());

  if (!uss_container->gcm_tag() || !uss_container->gcm_tag()->size()) {
    LOG(ERROR) << "UserSecretStash has empty AES-GCM tag";
    return false;
  }
  if (uss_container->gcm_tag()->size() != kAesGcmTagSize) {
    LOG(ERROR) << "UserSecretStash has AES-GCM tag of wrong length: "
               << uss_container->gcm_tag()->size()
               << ", expected: " << kAesGcmTagSize;
    return false;
  }
  tag->assign(uss_container->gcm_tag()->begin(),
              uss_container->gcm_tag()->end());

  if (uss_container->wrapped_key_blocks()) {
    *wrapped_key_blocks = LoadUserSecretStashWrappedKeyBlocks(
        *uss_container->wrapped_key_blocks());
  }

  return true;
}

std::optional<brillo::SecureBlob> UnwrapMainKeyFromBlocks(
    const std::map<std::string, UserSecretStash::WrappedKeyBlock>&
        wrapped_key_blocks,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (wrapping_id.empty()) {
    NOTREACHED() << "Empty wrapping ID is passed for UserSecretStash main key "
                    "unwrapping.";
    return std::nullopt;
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key unwrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return std::nullopt;
  }

  // Find the wrapped key block.
  const auto wrapped_key_block_iter = wrapped_key_blocks.find(wrapping_id);
  if (wrapped_key_block_iter == wrapped_key_blocks.end()) {
    LOG(ERROR)
        << "UserSecretStash wrapped key block with the given ID not found.";
    return std::nullopt;
  }
  const UserSecretStash::WrappedKeyBlock& wrapped_key_block =
      wrapped_key_block_iter->second;

  // Verify the wrapped key block format. No NOTREACHED() checks here, since the
  // key block is a deserialization of the persisted blob.
  if (wrapped_key_block.encryption_algorithm !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStash wrapped main key uses unknown algorithm: "
               << static_cast<int>(wrapped_key_block.encryption_algorithm)
               << ".";
    return std::nullopt;
  }
  if (wrapped_key_block.encrypted_key.empty()) {
    LOG(ERROR) << "UserSecretStash wrapped main key has empty encrypted key.";
    return std::nullopt;
  }
  if (wrapped_key_block.iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash wrapped main key has IV of wrong length: "
               << wrapped_key_block.iv.size() << ", expected: " << kAesGcmIVSize
               << ".";
    return std::nullopt;
  }
  if (wrapped_key_block.gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR)
        << "UserSecretStash wrapped main key has AES-GCM tag of wrong length: "
        << wrapped_key_block.gcm_tag.size() << ", expected: " << kAesGcmTagSize
        << ".";
    return std::nullopt;
  }

  // Attempt the unwrapping.
  brillo::SecureBlob main_key;
  if (!AesGcmDecrypt(wrapped_key_block.encrypted_key, /*ad=*/std::nullopt,
                     wrapped_key_block.gcm_tag, wrapping_key,
                     wrapped_key_block.iv, &main_key)) {
    LOG(ERROR) << "Failed to unwrap UserSecretStash main key";
    return std::nullopt;
  }
  return main_key;
}

}  // namespace

// static
std::unique_ptr<UserSecretStash> UserSecretStash::CreateRandom() {
  brillo::SecureBlob file_system_key =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_512_BIT_KEY_SIZE);
  brillo::SecureBlob reset_secret =
      CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH);
  // Note: make_unique() wouldn't work due to the constructor being private.
  return std::unique_ptr<UserSecretStash>(
      new UserSecretStash(file_system_key, reset_secret));
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::FromEncryptedContainer(
    const brillo::SecureBlob& flatbuffer, const brillo::SecureBlob& main_key) {
  if (main_key.size() != kAesGcm256KeySize) {
    LOG(ERROR) << "The UserSecretStash main key is of wrong length: "
               << main_key.size() << ", expected: " << kAesGcm256KeySize;
    return nullptr;
  }

  brillo::SecureBlob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  if (!LoadUserSecretStashContainer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                    &wrapped_key_blocks)) {
    // Note: the error is already logged.
    return nullptr;
  }

  return FromEncryptedPayload(ciphertext, iv, gcm_tag, wrapped_key_blocks,
                              main_key);
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::FromEncryptedPayload(
    const brillo::SecureBlob& ciphertext,
    const brillo::SecureBlob& iv,
    const brillo::SecureBlob& gcm_tag,
    const std::map<std::string, WrappedKeyBlock>& wrapped_key_blocks,
    const brillo::SecureBlob& main_key) {
  brillo::SecureBlob serialized_uss_payload;
  if (!AesGcmDecrypt(ciphertext, /*ad=*/std::nullopt, gcm_tag, main_key, iv,
                     &serialized_uss_payload)) {
    LOG(ERROR) << "Failed to decrypt UserSecretStash payload";
    return nullptr;
  }

  flatbuffers::Verifier payload_verifier(serialized_uss_payload.data(),
                                         serialized_uss_payload.size());
  if (!VerifyUserSecretStashPayloadBuffer(payload_verifier)) {
    LOG(ERROR) << "The UserSecretStashPayload flatbuffer is invalid";
    return nullptr;
  }

  auto uss_payload = GetUserSecretStashPayload(serialized_uss_payload.data());

  if (!uss_payload->file_system_key() ||
      !uss_payload->file_system_key()->size()) {
    LOG(ERROR) << "UserSecretStashPayload has no file system key";
    return nullptr;
  }
  brillo::SecureBlob file_system_key(uss_payload->file_system_key()->begin(),
                                     uss_payload->file_system_key()->end());

  if (!uss_payload->reset_secret() || !uss_payload->reset_secret()->size()) {
    LOG(ERROR) << "UserSecretStashPayload has no reset secret";
    return nullptr;
  }
  brillo::SecureBlob reset_secret(uss_payload->reset_secret()->begin(),
                                  uss_payload->reset_secret()->end());

  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(
      new UserSecretStash(file_system_key, reset_secret));
  stash->wrapped_key_blocks_ = std::move(wrapped_key_blocks);

  return stash;
}

// static
std::unique_ptr<UserSecretStash>
UserSecretStash::FromEncryptedContainerWithWrappingKey(
    const brillo::SecureBlob& flatbuffer,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    brillo::SecureBlob* main_key) {
  brillo::SecureBlob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  if (!LoadUserSecretStashContainer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                    &wrapped_key_blocks)) {
    // Note: the error is already logged.
    return nullptr;
  }

  std::optional<brillo::SecureBlob> main_key_optional =
      UnwrapMainKeyFromBlocks(wrapped_key_blocks, wrapping_id, wrapping_key);
  if (!main_key_optional) {
    // Note: the error is already logged.
    return nullptr;
  }

  std::unique_ptr<UserSecretStash> stash = FromEncryptedPayload(
      ciphertext, iv, gcm_tag, wrapped_key_blocks, *main_key_optional);
  if (!stash) {
    // Note: the error is already logged.
    return nullptr;
  }
  *main_key = *main_key_optional;
  return stash;
}

// static
brillo::SecureBlob UserSecretStash::CreateRandomMainKey() {
  return CreateSecureRandomBlob(kAesGcm256KeySize);
}

const brillo::SecureBlob& UserSecretStash::GetFileSystemKey() const {
  return file_system_key_;
}

void UserSecretStash::SetFileSystemKey(const brillo::SecureBlob& key) {
  file_system_key_ = key;
}

const brillo::SecureBlob& UserSecretStash::GetResetSecret() const {
  return reset_secret_;
}

void UserSecretStash::SetResetSecret(const brillo::SecureBlob& secret) {
  reset_secret_ = secret;
}

bool UserSecretStash::HasWrappedMainKey(const std::string& wrapping_id) const {
  return wrapped_key_blocks_.count(wrapping_id);
}

std::optional<brillo::SecureBlob> UserSecretStash::UnwrapMainKey(
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) const {
  return UnwrapMainKeyFromBlocks(wrapped_key_blocks_, wrapping_id,
                                 wrapping_key);
}

bool UserSecretStash::AddWrappedMainKey(
    const brillo::SecureBlob& main_key,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (main_key.empty()) {
    NOTREACHED() << "Empty UserSecretStash main key is passed for wrapping.";
    return false;
  }
  if (wrapping_id.empty()) {
    NOTREACHED()
        << "Empty wrapping ID is passed for UserSecretStash main key wrapping.";
    return false;
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key wrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return false;
  }

  // Protect from duplicate wrapping IDs.
  if (wrapped_key_blocks_.count(wrapping_id)) {
    LOG(ERROR) << "A UserSecretStash main key with the given wrapping_id "
                  "already exists.";
    return false;
  }

  // Perform the wrapping.
  WrappedKeyBlock wrapped_key_block;
  wrapped_key_block.encryption_algorithm =
      UserSecretStashEncryptionAlgorithm::AES_GCM_256;
  if (!AesGcmEncrypt(main_key, /*ad=*/std::nullopt, wrapping_key,
                     &wrapped_key_block.iv, &wrapped_key_block.gcm_tag,
                     &wrapped_key_block.encrypted_key)) {
    LOG(ERROR) << "Failed to wrap UserSecretStash main key.";
    return false;
  }

  wrapped_key_blocks_[wrapping_id] = std::move(wrapped_key_block);
  return true;
}

bool UserSecretStash::RemoveWrappedMainKey(const std::string& wrapping_id) {
  auto iter = wrapped_key_blocks_.find(wrapping_id);
  if (iter == wrapped_key_blocks_.end()) {
    LOG(ERROR) << "No UserSecretStash wrapped key block is found with the "
                  "given wrapping ID.";
    return false;
  }
  wrapped_key_blocks_.erase(iter);
  return true;
}

std::optional<brillo::SecureBlob> UserSecretStash::GetEncryptedContainer(
    const brillo::SecureBlob& main_key) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/4096, &allocator,
                                         /*own_allocator=*/false);

  auto fs_key_vector =
      builder.CreateVector(file_system_key_.data(), file_system_key_.size());
  auto reset_secret_vector =
      builder.CreateVector(reset_secret_.data(), reset_secret_.size());

  UserSecretStashPayloadBuilder uss_builder(builder);
  uss_builder.add_file_system_key(fs_key_vector);
  uss_builder.add_reset_secret(reset_secret_vector);
  auto uss = uss_builder.Finish();

  builder.Finish(uss);

  brillo::SecureBlob serialized_uss(
      builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());

  brillo::SecureBlob tag, iv, ciphertext;
  if (!AesGcmEncrypt(serialized_uss, /*ad=*/std::nullopt, main_key, &iv, &tag,
                     &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return std::nullopt;
  }

  builder.Clear();

  // Note: It can happen that the USS container is created with empty
  // |wrapped_key_blocks_| - they may be added later, when the user registers
  // the first credential with their cryptohome.
  return GenerateUserSecretStashContainer(ciphertext, tag, iv,
                                          wrapped_key_blocks_);
}

UserSecretStash::UserSecretStash(const brillo::SecureBlob& file_system_key,
                                 const brillo::SecureBlob& reset_secret)
    : file_system_key_(file_system_key), reset_secret_(reset_secret) {
  CHECK(!file_system_key_.empty());
  CHECK(!reset_secret_.empty());
}

}  // namespace cryptohome
