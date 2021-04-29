// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"
#include "cryptohome/user_secret_stash_generated.h"

namespace cryptohome {

namespace {

brillo::SecureBlob GenerateAesGcmEncryptedUSS(
    const brillo::SecureBlob& ciphertext,
    const brillo::SecureBlob& tag,
    const brillo::SecureBlob& iv) {
  std::unique_ptr<flatbuffers::Allocator> allocator =
      std::make_unique<FlatbufferSecureAllocatorBridge>();
  flatbuffers::FlatBufferBuilder builder(4096, allocator.get(),
                                         /*own_allocator=*/false);

  auto ciphertext_vector =
      builder.CreateVector(ciphertext.data(), ciphertext.size());
  auto tag_vector = builder.CreateVector(tag.data(), tag.size());
  auto iv_vector = builder.CreateVector(iv.data(), iv.size());

  AesGcmEncryptedUSSBuilder encrypted_uss_builder(builder);
  encrypted_uss_builder.add_ciphertext(ciphertext_vector);
  encrypted_uss_builder.add_tag(tag_vector);
  encrypted_uss_builder.add_iv(iv_vector);
  auto encrypted_uss = encrypted_uss_builder.Finish();

  builder.Finish(encrypted_uss);

  auto ret_val =
      brillo::SecureBlob(builder.GetBufferPointer(),
                         builder.GetBufferPointer() + builder.GetSize());

  builder.Clear();

  return ret_val;
}

}  // namespace

const brillo::SecureBlob& UserSecretStash::GetFileSystemKey() const {
  return file_system_key_.value();
}

void UserSecretStash::SetFileSystemKey(const brillo::SecureBlob& key) {
  file_system_key_ = key;
}

const brillo::SecureBlob& UserSecretStash::GetResetSecret() const {
  return reset_secret_.value();
}

void UserSecretStash::SetResetSecret(const brillo::SecureBlob& secret) {
  reset_secret_ = secret;
}

void UserSecretStash::InitializeRandom() {
  file_system_key_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_512_BIT_KEY_SIZE);
  reset_secret_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_RESET_SECRET_LENGTH);
}

base::Optional<brillo::SecureBlob> UserSecretStash::GetAesGcmEncrypted(
    const brillo::SecureBlob& main_key) {
  std::unique_ptr<flatbuffers::Allocator> allocator =
      std::make_unique<FlatbufferSecureAllocatorBridge>();
  flatbuffers::FlatBufferBuilder builder(4096, allocator.get(),
                                         /*own_allocator=*/false);

  auto fs_key_vector =
      builder.CreateVector(file_system_key_->data(), file_system_key_->size());
  auto reset_secret_vector =
      builder.CreateVector(reset_secret_->data(), reset_secret_->size());

  UserSecretStashBufBuilder uss_builder(builder);
  uss_builder.add_file_system_key(fs_key_vector);
  uss_builder.add_reset_secret(reset_secret_vector);
  auto uss = uss_builder.Finish();

  builder.Finish(uss);

  brillo::SecureBlob serialized_uss(
      builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());

  brillo::SecureBlob tag, iv, ciphertext;
  if (!CryptoLib::AesGcmEncrypt(serialized_uss, main_key, &iv, &tag,
                                &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return base::nullopt;
  }

  builder.Clear();

  return base::Optional<brillo::SecureBlob>(
      GenerateAesGcmEncryptedUSS(ciphertext, tag, iv));
}

}  // namespace cryptohome
