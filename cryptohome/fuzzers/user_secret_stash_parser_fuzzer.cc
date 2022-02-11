// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <openssl/err.h>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/fuzzers/blob_mutator.h"
#include "cryptohome/user_secret_stash.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;
using cryptohome::UserSecretStash;
using cryptohome::UserSecretStashContainer;
using cryptohome::UserSecretStashPayload;

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
  // Create USS payload.
  UserSecretStashPayload uss_payload_struct;
  uss_payload_struct.fek =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.fnek =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.fek_salt =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.fnek_salt =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.fek_sig =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.fnek_sig =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());
  uss_payload_struct.reset_secret =
      SecureBlob(fuzzed_data_provider->ConsumeRandomLengthString());

  // Serialize the USS payload to flatbuffer and mutate it.
  std::optional<SecureBlob> uss_payload_optional =
      uss_payload_struct.Serialize();
  CHECK(uss_payload_optional.has_value());
  Blob uss_payload(uss_payload_optional.value().begin(),
                   uss_payload_optional.value().end());
  Blob mutated_uss_payload = MutateBlob(
      uss_payload, /*min_length=*/1, /*max_length=*/1000, fuzzed_data_provider);

  // Pick up a "random" AES-GCM USS main key. Note that `AesGcmEncrypt()`
  // requires the key to be of exact size.
  Blob uss_main_key = fuzzed_data_provider->ConsumeBytes<uint8_t>(
      cryptohome::kAesGcm256KeySize);
  uss_main_key.resize(cryptohome::kAesGcm256KeySize);

  // Encrypt the mutated USS payload flatbuffer.
  SecureBlob iv, tag, ciphertext;
  CHECK(cryptohome::AesGcmEncrypt(SecureBlob(mutated_uss_payload),
                                  /*ad=*/std::nullopt, SecureBlob(uss_main_key),
                                  &iv, &tag, &ciphertext));

  // Create USS container from mutated fields.
  UserSecretStashContainer uss_container_struct;
  uss_container_struct.encryption_algorithm =
      cryptohome::UserSecretStashEncryptionAlgorithm::AES_GCM_256;
  uss_container_struct.ciphertext = SecureBlob(
      MutateBlob(Blob(ciphertext.begin(), ciphertext.end()),
                 /*min_length=*/0, /*max_length=*/1000, fuzzed_data_provider));
  uss_container_struct.iv = SecureBlob(
      MutateBlob(Blob(iv.begin(), iv.end()),
                 /*min_length=*/0, /*max_length=*/1000, fuzzed_data_provider));
  uss_container_struct.gcm_tag =
      SecureBlob(MutateBlob(Blob(tag.begin(), tag.end()), /*min_length=*/0,
                            /*max_length=*/1000, fuzzed_data_provider));

  // Serialize the USS container to flatbuffer and mutate it.
  std::optional<SecureBlob> uss_container_optional =
      uss_container_struct.Serialize();
  CHECK(uss_container_optional.has_value());
  Blob uss_container(uss_container_optional.value().begin(),
                     uss_container_optional.value().end());
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
  CHECK(first.GetFileSystemKeyset().Key().fek ==
        second.GetFileSystemKeyset().Key().fek);
  CHECK(first.GetFileSystemKeyset().Key().fnek ==
        second.GetFileSystemKeyset().Key().fnek);
  CHECK(first.GetFileSystemKeyset().Key().fek_salt ==
        second.GetFileSystemKeyset().Key().fek_salt);
  CHECK(first.GetFileSystemKeyset().Key().fnek_salt ==
        second.GetFileSystemKeyset().Key().fnek_salt);
  CHECK(first.GetFileSystemKeyset().KeyReference().fek_sig ==
        second.GetFileSystemKeyset().KeyReference().fek_sig);
  CHECK(first.GetFileSystemKeyset().KeyReference().fnek_sig ==
        second.GetFileSystemKeyset().KeyReference().fnek_sig);
  CHECK(first.GetFileSystemKeyset().chaps_key() ==
        second.GetFileSystemKeyset().chaps_key());
  CHECK(first.GetResetSecret() == second.GetResetSecret());
  CHECK_EQ(first.GetCreatedOnOsVersion(), second.GetCreatedOnOsVersion());
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
    std::optional<SecureBlob> reencrypted =
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
