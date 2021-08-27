// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"
#include "cryptohome/user_secret_stash_container_generated.h"
#include "cryptohome/user_secret_stash_payload_generated.h"

namespace cryptohome {

namespace {

brillo::SecureBlob GenerateAesGcmEncryptedUSS(
    const brillo::SecureBlob& ciphertext,
    const brillo::SecureBlob& tag,
    const brillo::SecureBlob& iv) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/4096, &allocator,
                                         /*own_allocator=*/false);

  auto ciphertext_vector =
      builder.CreateVector(ciphertext.data(), ciphertext.size());
  auto tag_vector = builder.CreateVector(tag.data(), tag.size());
  auto iv_vector = builder.CreateVector(iv.data(), iv.size());

  UserSecretStashContainerBuilder uss_container_builder(builder);
  uss_container_builder.add_encryption_algorithm(
      UserSecretStashEncryptionAlgorithm::AES_GCM_256);
  uss_container_builder.add_ciphertext(ciphertext_vector);
  uss_container_builder.add_aes_gcm_tag(tag_vector);
  uss_container_builder.add_iv(iv_vector);
  auto uss_container = uss_container_builder.Finish();

  builder.Finish(uss_container);

  auto ret_val =
      brillo::SecureBlob(builder.GetBufferPointer(),
                         builder.GetBufferPointer() + builder.GetSize());

  builder.Clear();

  return ret_val;
}

}  // namespace

// static
std::unique_ptr<UserSecretStash> UserSecretStash::CreateRandom() {
  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(new UserSecretStash);
  stash->file_system_key_ =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_512_BIT_KEY_SIZE);
  stash->reset_secret_ = CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH);
  return stash;
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::FromEncryptedContainer(
    const brillo::SecureBlob& flatbuffer, const brillo::SecureBlob& main_key) {
  if (main_key.size() != kAesGcm256KeySize) {
    LOG(ERROR) << "The UserSecretStash main key is of wrong length: "
               << main_key.size() << ", expected: " << kAesGcm256KeySize;
    return nullptr;
  }

  flatbuffers::Verifier payload_verifier(flatbuffer.data(), flatbuffer.size());
  if (!VerifyUserSecretStashContainerBuffer(payload_verifier)) {
    LOG(ERROR) << "The UserSecretStashContainer flatbuffer is invalid";
    return nullptr;
  }

  auto uss_container = GetUserSecretStashContainer(flatbuffer.data());

  UserSecretStashEncryptionAlgorithm algorithm =
      uss_container->encryption_algorithm();
  if (algorithm != UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStashContainer uses unknown algorithm: "
               << static_cast<int>(algorithm);
    return nullptr;
  }

  if (!uss_container->ciphertext() || !uss_container->ciphertext()->size()) {
    LOG(ERROR) << "UserSecretStash has empty ciphertext";
    return nullptr;
  }
  brillo::SecureBlob ciphertext(uss_container->ciphertext()->begin(),
                                uss_container->ciphertext()->end());

  if (!uss_container->iv() || !uss_container->iv()->size()) {
    LOG(ERROR) << "UserSecretStash has empty IV";
    return nullptr;
  }
  if (uss_container->iv()->size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash has IV of wrong length: "
               << uss_container->iv()->size()
               << ", expected: " << kAesGcmIVSize;
    return nullptr;
  }
  brillo::SecureBlob iv(uss_container->iv()->begin(),
                        uss_container->iv()->end());

  if (!uss_container->aes_gcm_tag() || !uss_container->aes_gcm_tag()->size()) {
    LOG(ERROR) << "UserSecretStash has empty AES-GCM tag";
    return nullptr;
  }
  if (uss_container->aes_gcm_tag()->size() != kAesGcmTagSize) {
    LOG(ERROR) << "UserSecretStash has AES-GCM tag of wrong length: "
               << uss_container->aes_gcm_tag()->size()
               << ", expected: " << kAesGcmTagSize;
    return nullptr;
  }
  brillo::SecureBlob tag(uss_container->aes_gcm_tag()->begin(),
                         uss_container->aes_gcm_tag()->end());

  brillo::SecureBlob serialized_uss;
  if (!AesGcmDecrypt(ciphertext, /*ad=*/base::nullopt, tag, main_key, iv,
                     &serialized_uss)) {
    LOG(ERROR) << "Failed to decrypt UserSecretStash";
    return nullptr;
  }

  flatbuffers::Verifier uss_verifier(serialized_uss.data(),
                                     serialized_uss.size());
  if (!VerifyUserSecretStashPayloadBuffer(uss_verifier)) {
    LOG(ERROR) << "The UserSecretStashPayload flatbuffer is invalid";
    return nullptr;
  }

  auto uss = GetUserSecretStashPayload(serialized_uss.data());
  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(new UserSecretStash);

  if (uss->file_system_key() && uss->file_system_key()->size()) {
    stash->file_system_key_ = brillo::SecureBlob(
        uss->file_system_key()->begin(), uss->file_system_key()->end());
  }

  if (uss->reset_secret() && uss->reset_secret()->size()) {
    stash->reset_secret_ = brillo::SecureBlob(uss->reset_secret()->begin(),
                                              uss->reset_secret()->end());
  }

  return stash;
}

bool UserSecretStash::HasFileSystemKey() const {
  return file_system_key_.has_value();
}

const brillo::SecureBlob& UserSecretStash::GetFileSystemKey() const {
  return file_system_key_.value();
}

void UserSecretStash::SetFileSystemKey(const brillo::SecureBlob& key) {
  file_system_key_ = key;
}

bool UserSecretStash::HasResetSecret() const {
  return reset_secret_.has_value();
}

const brillo::SecureBlob& UserSecretStash::GetResetSecret() const {
  return reset_secret_.value();
}

void UserSecretStash::SetResetSecret(const brillo::SecureBlob& secret) {
  reset_secret_ = secret;
}

base::Optional<brillo::SecureBlob> UserSecretStash::GetEncryptedContainer(
    const brillo::SecureBlob& main_key) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(/*initial_size=*/4096, &allocator,
                                         /*own_allocator=*/false);

  auto fs_key_vector =
      builder.CreateVector(file_system_key_->data(), file_system_key_->size());
  auto reset_secret_vector =
      builder.CreateVector(reset_secret_->data(), reset_secret_->size());

  UserSecretStashPayloadBuilder uss_builder(builder);
  uss_builder.add_file_system_key(fs_key_vector);
  uss_builder.add_reset_secret(reset_secret_vector);
  auto uss = uss_builder.Finish();

  builder.Finish(uss);

  brillo::SecureBlob serialized_uss(
      builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());

  brillo::SecureBlob tag, iv, ciphertext;
  if (!AesGcmEncrypt(serialized_uss, /*ad=*/base::nullopt, main_key, &iv, &tag,
                     &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return base::nullopt;
  }

  builder.Clear();

  return GenerateAesGcmEncryptedUSS(ciphertext, tag, iv);
}

}  // namespace cryptohome
