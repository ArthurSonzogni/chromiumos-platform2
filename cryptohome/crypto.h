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

#include "cryptohome/auth_block_state.pb.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

struct KeyBlobs;
struct WrappedKeyMaterial;
class VaultKeyset;
class AuthBlock;

class Crypto {
 public:
  // Default constructor
  explicit Crypto(Platform* platform);
  Crypto(const Crypto&) = delete;
  Crypto& operator=(const Crypto&) = delete;

  virtual ~Crypto();

  // Initializes Crypto
  virtual bool Init(TpmInit* tpm_init);

  // Decrypts an encrypted vault keyset.  The vault keyset should be the output
  // of EncryptVaultKeyset().
  //
  // Parameters
  //   encrypted_keyset - The blob containing the encrypted keyset
  //   vault_key - The passkey used to decrypt the keyset
  //   crypt_flags (OUT) - Whether the keyset was wrapped by the TPM or scrypt
  //   locked_to_single_user - Whether the device has transitioned into
  //   user-specific modality by extending PCR4 with a user-specific value.
  //   error (OUT) - The specific error code on failure
  //   vault_keyset (OUT) - The decrypted vault keyset on success
  virtual bool DecryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                  const brillo::SecureBlob& vault_key,
                                  bool locked_to_single_user,
                                  unsigned int* crypt_flags,
                                  CryptoError* error,
                                  VaultKeyset* vault_keyset);

  // Encrypts the vault keyset with the given passkey
  //
  // Parameters
  //   vault_keyset - The VaultKeyset to encrypt
  //   vault_key - The passkey used to encrypt the keyset
  //   vault_key_salt - The salt to use for the vault passkey to key conversion
  //                    when encrypting the keyset
  //   obfuscated_username - The value of username obfuscated. It's the same
  //                         value used as the folder name where the user data
  //                         is stored.
  //   auth_block_state - On success, the plaintext state needed to initialize
  //                      the auth block.
  //   wrapped_key_material - On success, the encrypted vault keys.
  virtual bool EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                  const brillo::SecureBlob& vault_key,
                                  const brillo::SecureBlob& vault_key_salt,
                                  const std::string& obfuscated_username,
                                  AuthBlockState* auth_block_state,
                                  WrappedKeyMaterial* wrapped) const;

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

  // Seals arbitrary-length data to the TPM's PCR0.
  // Parameters
  //   data - Data to encrypt with tpm.
  //   encrypted_data (OUT) - Encrypted data as a string.
  // Returns true if we succeeded in creating the encrypted data blob.
  virtual bool EncryptWithTpm(const brillo::SecureBlob& data,
                              std::string* encrypted_data) const;

  // Decrypts data previously sealed to the TPM's PCR0.
  // Parameters
  //   encrypted_data - Encrypted data previously sealed with EncryptWithTPM.
  //   data (OUT) - Decrypted data as a blob.
  // Returns true if we succeeded to decrypt the data blob.
  virtual bool DecryptWithTpm(const std::string& encrypted_data,
                              brillo::SecureBlob* data) const;

  // Note the following 4 methods are only to be used if there is a strong
  // reason to avoid talking to the TPM e.g. needing to flush some encrypted
  // data periodically to disk and you don't want to seal a key each time.
  // Otherwise, a user should use Encrypt/DecryptWithTpm.

  // Creates a randomly generated aes key and seals it to the TPM's PCR0.
  virtual bool CreateSealedKey(brillo::SecureBlob* aes_key,
                               brillo::SecureBlob* sealed_key) const;

  // Encrypts the given data using the aes_key. Sealed key is necessary to
  // wrap into the returned data to allow for decryption.
  virtual bool EncryptData(const brillo::SecureBlob& data,
                           const brillo::SecureBlob& aes_key,
                           const brillo::SecureBlob& sealed_key,
                           std::string* encrypted_data) const;

  // Returns the sealed and unsealed aes_key wrapped in the encrypted_data.
  virtual bool UnsealKey(const std::string& encrypted_data,
                         brillo::SecureBlob* aes_key,
                         brillo::SecureBlob* sealed_key) const;

  // Decrypts encrypted_data using the aes_key.
  virtual bool DecryptData(const std::string& encrypted_data,
                           const brillo::SecureBlob& aes_key,
                           brillo::SecureBlob* data) const;

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

  // Sets the TPM implementation
  void set_tpm(Tpm* value) { tpm_ = value; }

  // Gets whether the TPM is set
  bool has_tpm() const { return (tpm_ != NULL); }

  // Gets the TPM implementation
  Tpm* get_tpm() { return tpm_; }

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
  bool EncryptScrypt(const VaultKeyset& vault_keyset,
                     const brillo::SecureBlob& key,
                     SerializedVaultKeyset* serialized) const;

  bool EncryptChallengeCredential(const VaultKeyset& vault_keyset,
                                  const brillo::SecureBlob& key,
                                  const std::string& obfuscated_username,
                                  SerializedVaultKeyset* serialized) const;

  // This function consumes the Vault Keyset Key (VKK) and IV, and produces the
  // unwrapped secrets from the Vault Keyset.
  // |serialized| is the serialized vault keyset protobuf.
  // |vkk_data| is the VKK and the VKK IV.
  // |keyset| is the C++ class populated with the |serialized| protobuf.
  // |error| is populated upon failure.
  //
  // Returns true on success, and false on failure.
  bool UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                         const KeyBlobs& vkk_data,
                         VaultKeyset* keyset,
                         CryptoError* error);

  bool DecryptScrypt(const SerializedVaultKeyset& serialized,
                     const brillo::SecureBlob& key,
                     CryptoError* error,
                     VaultKeyset* keyset) const;

  bool DecryptChallengeCredential(const SerializedVaultKeyset& serialized,
                                  const brillo::SecureBlob& key,
                                  CryptoError* error,
                                  VaultKeyset* vault_keyset) const;

  bool EncryptAuthorizationData(SerializedVaultKeyset* serialized,
                                const brillo::SecureBlob& vkk_key,
                                const brillo::SecureBlob& vkk_iv) const;

  // This function serves as a factory method to return the authblock used in
  // authentication creation.
  // |serialized_key_flags| is the flag data stores in the vault keyset which
  // helps determine the type of AuthBlock.
  std::unique_ptr<AuthBlock> CreateAuthBlock(const VaultKeyset& vk) const;

  // This function serves as a factory method to return the authblock used in
  // authentication derivation.
  // |serialized_key_flags| is the flag data stores in the vault keyset which
  // helps determine the type of AuthBlock.
  std::unique_ptr<AuthBlock> DeriveAuthBlock(int serialized_key_flags);

  // The TPM implementation
  Tpm* tpm_;

  // Platform abstraction
  Platform* platform_;

  // The TpmInit object used to reload Cryptohome key
  TpmInit* tpm_init_;

  // Handler for Low Entropy credentials.
  std::unique_ptr<LECredentialManager> le_manager_;

  bool disable_logging_for_tests_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_H_
