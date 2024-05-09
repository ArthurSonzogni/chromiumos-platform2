// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/file_system_keyset.h"

#include <utility>

#include <brillo/cryptohome.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/cryptohome_common.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::CreateSecureRandomBlob;

}

FileSystemKeyset FileSystemKeyset::CreateRandom() {
  const libstorage::FileSystemKey key = {
      .fek = CreateSecureRandomBlob(kCryptohomeDefaultKeySize),
      .fnek = CreateSecureRandomBlob(kCryptohomeDefaultKeySize),
      .fek_salt = CreateSecureRandomBlob(kCryptohomeDefaultKeySaltSize),
      .fnek_salt = CreateSecureRandomBlob(kCryptohomeDefaultKeySaltSize),
  };

  const libstorage::FileSystemKeyReference key_reference = {
      .fek_sig = CreateSecureRandomBlob(kCryptohomeDefaultKeySignatureSize),
      .fnek_sig = CreateSecureRandomBlob(kCryptohomeDefaultKeySignatureSize),
  };

  const brillo::SecureBlob chaps_key =
      CreateSecureRandomBlob(kCryptohomeChapsKeyLength);

  return FileSystemKeyset(key, key_reference, chaps_key);
}

FileSystemKeyset::FileSystemKeyset() = default;

FileSystemKeyset::FileSystemKeyset(
    libstorage::FileSystemKey key,
    libstorage::FileSystemKeyReference key_reference,
    brillo::SecureBlob chaps_key)
    : key_(std::move(key)),
      key_reference_(std::move(key_reference)),
      chaps_key_(std::move(chaps_key)) {}

const libstorage::FileSystemKey& FileSystemKeyset::Key() const {
  return key_;
}

const libstorage::FileSystemKeyReference& FileSystemKeyset::KeyReference()
    const {
  return key_reference_;
}

const brillo::SecureBlob& FileSystemKeyset::chaps_key() const {
  return chaps_key_;
}

}  // namespace cryptohome
