// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Crypto - class for handling the keyset key management functions relating to
// cryptohome.  This includes wrapping/unwrapping the vault keyset (and
// supporting functions) and setting/clearing the user keyring for use with
// ecryptfs.

#ifndef CRYPTOHOME_CRYPTO_H_
#define CRYPTOHOME_CRYPTO_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

struct KeyBlobs;
class AuthBlock;
class VaultKeyset;

class Crypto {
 public:
  // Default constructor
  explicit Crypto(Platform* platform);
  Crypto(const Crypto&) = delete;
  Crypto& operator=(const Crypto&) = delete;

  virtual ~Crypto();

  // Initializes Crypto
  virtual bool Init(Tpm* tpm, CryptohomeKeysManager* cryptohome_keys_manager);

  // Gets an existing salt, or creates one if it doesn't exist
  //
  // Parameters
  //   path - The path to the salt file
  //   length - The length of the new salt if it needs to be created
  //   force - If true, forces creation of a new salt even if the file exists
  //   salt (OUT) - The salt
  virtual bool GetOrCreateSalt(const base::FilePath& path,
                               size_t length,
                               bool force,
                               brillo::SecureBlob* salt) const;

  // Converts a null-terminated password to a passkey (ascii-encoded first half
  // of the salted SHA1 hash of the password).
  //
  // Parameters
  //   password - The password to convert
  //   salt - The salt used during hashing
  //   passkey (OUT) - The passkey
  static void PasswordToPasskey(const char* password,
                                const brillo::SecureBlob& salt,
                                brillo::SecureBlob* passkey);

  // Ensures that the TPM is connected
  virtual CryptoError EnsureTpm(bool reload_key) const;

  // Attempts to reset an LE credential, specified by |vk|.
  // Returns true on success.
  // On failure, false is returned and |error| is set with the appropriate
  // error.
  bool ResetLECredential(const VaultKeyset& vk_reset,
                         const VaultKeyset& vk,
                         CryptoError* error) const;

  // Removes an LE credential specified by |label|.
  // Returns true on success, false otherwise.
  bool RemoveLECredential(uint64_t label) const;

  // Returns whether the provided label needs valid PCR criteria attached.
  bool NeedsPcrBinding(const uint64_t& label) const;

  // Returns whether TPM unseal operations with direct authorization are allowed
  // on this device. Some devices cannot reset the dictionary attack counter.
  // And if unseal is performed with wrong authorization value, the counter
  // increases which might eventually temporary block the TPM. To avoid this
  // we don't allow the unseal with authorization. For details see
  // https://buganizer.corp.google.com/issues/127321828.
  bool CanUnsealWithUserAuth() const;

  // Returns the number of wrong authentication attempts for the LE keyset.
  int GetWrongAuthAttempts(uint64_t le_label) const;

  // Gets whether the TPM is set
  bool has_tpm() const { return (tpm_ != NULL); }

  // Gets the TPM implementation
  Tpm* tpm() { return tpm_; }

  // Gets the CryptohomeKeysManager object.
  CryptohomeKeysManager* cryptohome_keys_manager() {
    return cryptohome_keys_manager_;
  }

  // Gets an instance of the LECredentialManagerImpl object.
  LECredentialManager* le_manager() { return le_manager_.get(); }

  // Checks if the cryptohome key is loaded in TPM
  bool is_cryptohome_key_loaded() const;

  // Sets the Platform implementation
  // Does NOT take ownership of the pointer.
  void set_platform(Platform* value) { platform_ = value; }

  Platform* platform() { return platform_; }

  void set_disable_logging_for_testing(bool disable) {
    disable_logging_for_tests_ = disable;
  }

  void set_le_manager_for_testing(
      std::unique_ptr<LECredentialManager> le_manager) {
    le_manager_ = std::move(le_manager);
  }

 private:
  // The TPM implementation
  Tpm* tpm_;

  // Platform abstraction
  Platform* platform_;

  // The CryptohomeKeysManager object used to reload Cryptohome keys
  CryptohomeKeysManager* cryptohome_keys_manager_;

  // Handler for Low Entropy credentials.
  std::unique_ptr<LECredentialManager> le_manager_;

  bool disable_logging_for_tests_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_H_
