// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_
#define CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_

#include <map>
#include <string>
#include <tuple>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/encrypted.h"

namespace cryptohome {

// This class represents a decrypted User Secret Stash (USS). It is built around
// the encrypted version of the class but it also has access to all of the
// decrypted secrets.
class DecryptedUss {
 public:
  // Attempt to decrypt USS using using the main key.
  static CryptohomeStatusOr<DecryptedUss> FromBlobUsingMainKey(
      const brillo::Blob& flatbuffer, const brillo::SecureBlob& main_key);

  // Attempt to decrypt USS using using a wrapped key. On success this also
  // returns the main key.
  static CryptohomeStatusOr<std::tuple<DecryptedUss, brillo::SecureBlob>>
  FromBlobUsingWrappedKey(const brillo::Blob& flatbuffer,
                          const std::string& wrapping_id,
                          const brillo::SecureBlob& wrapping_key);

  DecryptedUss(const DecryptedUss&) = delete;
  DecryptedUss& operator=(const DecryptedUss&) = delete;
  DecryptedUss(DecryptedUss&&) = default;
  DecryptedUss& operator=(DecryptedUss&&) = default;

  // Extracts all of the contents of this object into an external set of
  // variables. This invalidates any existing contents.
  //
  // TODO(b/295898583): Remove this once UserSecretStash can use DecryptedUss
  // directly instead of only using it for construction.
  void ExtractContents(
      FileSystemKeyset& file_system_keyset,
      std::map<std::string, EncryptedUss::WrappedKeyBlock>& wrapped_key_blocks,
      std::string& created_on_os_version,
      std::map<std::string, brillo::SecureBlob>& reset_secrets,
      std::map<AuthFactorType, brillo::SecureBlob>& rate_limiter_reset_secrets,
      UserMetadata& user_metadata) &&;

 private:
  // Given an EncryptedUss and a main key, attempt to decrypt it and construct
  // the DecryptedUss.
  static CryptohomeStatusOr<DecryptedUss> FromEncryptedUss(
      EncryptedUss encrypted, const brillo::SecureBlob& main_key);

  DecryptedUss(
      EncryptedUss encrypted,
      FileSystemKeyset file_system_keyset,
      std::map<std::string, brillo::SecureBlob> reset_secrets,
      std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets);

  // The underlying raw data.
  EncryptedUss encrypted_;
  // Keys registered with the kernel to decrypt files and file names, together
  // with corresponding salts and signatures.
  FileSystemKeyset file_system_keyset_;
  // The reset secrets corresponding to each auth factor, by label.
  std::map<std::string, brillo::SecureBlob> reset_secrets_;
  // The reset secrets corresponding to each auth factor type's rate limiter.
  std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_
