// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <openssl/err.h>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/fuzzers/blob_mutator.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_container_generated.h"
#include "cryptohome/user_secret_stash_payload_generated.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;
using cryptohome::UserSecretStash;

namespace {

// Performs the static initialization that's needed only once across all fuzzer
// runs.
class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

// Clears the OpenSSL error queue on destruction. Useful for preventing fuzzer
// memory leaks.
struct ScopedOpensslErrorClearer {
  ~ScopedOpensslErrorClearer() { ERR_clear_error(); }
};

// Generates mutated blobs of the USS container and the USS main key.
void PrepareMutatedArguments(FuzzedDataProvider* fuzzed_data_provider,
                             SecureBlob* mutated_uss_container,
                             SecureBlob* mutated_uss_main_key) {
  flatbuffers::FlatBufferBuilder builder;

  // Create USS payload.
  cryptohome::UserSecretStashPayloadT uss_payload_obj;
  uss_payload_obj.file_system_key =
      BlobFromString(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_obj.reset_secret =
      BlobFromString(fuzzed_data_provider->ConsumeRandomLengthString());

  // Serialize the USS payload to flatbuffer and mutate it.
  builder.Finish(
      cryptohome::UserSecretStashPayload::Pack(builder, &uss_payload_obj));
  Blob uss_payload(builder.GetBufferPointer(),
                   builder.GetBufferPointer() + builder.GetSize());
  builder.Clear();
  Blob mutated_uss_payload = MutateBlob(
      uss_payload, /*min_length=*/1, /*max_length=*/1000, fuzzed_data_provider);

  // Pick up a "random" AES-GCM USS main key. Note that `AesGcmEncrypt()`
  // requires the key to be of exact size.
  Blob uss_main_key = fuzzed_data_provider->ConsumeBytes<uint8_t>(
      cryptohome::kAesGcm256KeySize);
  uss_main_key.resize(cryptohome::kAesGcm256KeySize);

  // Encrypt the mutated USS payload flatbuffer.
  SecureBlob iv, tag, ciphertext;
  CHECK(cryptohome::AesGcmEncrypt(
      SecureBlob(mutated_uss_payload), /*ad=*/base::nullopt,
      SecureBlob(uss_main_key), &iv, &tag, &ciphertext));

  // Create USS container from mutated fields.
  cryptohome::UserSecretStashContainerT uss_container_obj;
  uss_container_obj.encryption_algorithm =
      cryptohome::UserSecretStashEncryptionAlgorithm::AES_GCM_256;
  uss_container_obj.ciphertext =
      MutateBlob(Blob(ciphertext.begin(), ciphertext.end()),
                 /*min_length=*/0, /*max_length=*/1000, fuzzed_data_provider);
  uss_container_obj.iv =
      MutateBlob(Blob(iv.begin(), iv.end()),
                 /*min_length=*/0, /*max_length=*/1000, fuzzed_data_provider);
  uss_container_obj.gcm_tag =
      MutateBlob(Blob(tag.begin(), tag.end()), /*min_length=*/0,
                 /*max_length=*/1000, fuzzed_data_provider);

  // Serialize the USS container to flatbuffer and mutate it.
  builder.Finish(
      cryptohome::UserSecretStashContainer::Pack(builder, &uss_container_obj));
  Blob uss_container(builder.GetBufferPointer(),
                     builder.GetBufferPointer() + builder.GetSize());
  builder.Clear();
  *mutated_uss_container =
      SecureBlob(MutateBlob(uss_container, /*min_length=*/0,
                            /*max_length=*/1000, fuzzed_data_provider));

  // Mutate the USS main key.
  *mutated_uss_main_key = SecureBlob(MutateBlob(
      uss_main_key, /*min_length=*/0,
      /*max_length=*/cryptohome::kAesGcm256KeySize, fuzzed_data_provider));
}

void AssertStashesEqual(const UserSecretStash& first,
                        const UserSecretStash& second) {
  CHECK(first.GetFileSystemKey() == second.GetFileSystemKey());
  CHECK(first.GetResetSecret() == second.GetResetSecret());
}

}  // namespace

// Fuzzes the |UserSecretStash::FromEncryptedContainer()| function.
// It starts of a semantically correct USS with a corresponding USS main key,
// and mutates all parameters before passing them to the tested function.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  // Prevent OpenSSL errors from accumulating in the error queue and leaking
  // memory across fuzzer executions.
  ScopedOpensslErrorClearer scoped_openssl_error_clearer;

  FuzzedDataProvider fuzzed_data_provider(data, size);

  SecureBlob mutated_uss_container, mutated_uss_main_key;
  PrepareMutatedArguments(&fuzzed_data_provider, &mutated_uss_container,
                          &mutated_uss_main_key);

  // The USS decryption may succeed or fail, but never crash.
  std::unique_ptr<UserSecretStash> stash =
      UserSecretStash::FromEncryptedContainer(mutated_uss_container,
                                              mutated_uss_main_key);

  if (stash) {
    // If the USS was decrypted successfully, its reencryption must succeed as
    // well.
    base::Optional<SecureBlob> reencrypted =
        stash->GetEncryptedContainer(mutated_uss_main_key);
    CHECK(reencrypted);

    // Decryption of the reencrypted USS must succeed as well, and the result
    // must be equal to the original USS.
    std::unique_ptr<UserSecretStash> stash2 =
        UserSecretStash::FromEncryptedContainer(*reencrypted,
                                                mutated_uss_main_key);
    CHECK(stash2);
    AssertStashesEqual(*stash, *stash2);
  }

  return 0;
}
