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
#include <brillo/secure_blob.h>

#include "cryptohome/le_credential_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

#include "vault_keyset.pb.h"  // NOLINT(build/include)

namespace cryptohome {

class VaultKeyset;

extern const char kSystemSaltFile[];

class Crypto {
 public:
  enum CryptoError {
    CE_NONE = 0,
    CE_TPM_FATAL,
    CE_TPM_COMM_ERROR,
    CE_TPM_DEFEND_LOCK,
    CE_TPM_CRYPTO,
    CE_TPM_REBOOT,
    CE_SCRYPT_CRYPTO,
    CE_OTHER_FATAL,
    CE_OTHER_CRYPTO,
    CE_NO_PUBLIC_KEY_HASH,
    // Low Entropy(LE) credential protection is not supported on this device.
    CE_LE_NOT_SUPPORTED,
    // The LE secret provided during decryption is invalid.
    CE_LE_INVALID_SECRET,
  };

  // Default constructor
  explicit Crypto(Platform* platform);

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
  //   is_pcr_extended - Whether the device has transitioned into user-specific
  //                     modality by extending PCR4 with a user-specific value.
  //   error (OUT) - The specific error code on failure
  //   vault_keyset (OUT) - The decrypted vault keyset on success
  virtual bool DecryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                  const brillo::SecureBlob& vault_key,
                                  bool is_pcr_extended,
                                  unsigned int* crypt_flags, CryptoError* error,
                                  VaultKeyset* vault_keyset) const;

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
  //   encrypted_keyset - On success, the encrypted vault keyset
  virtual bool EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                  const brillo::SecureBlob& vault_key,
                                  const brillo::SecureBlob& vault_key_salt,
                                  const std::string& obfuscated_username,
                                  SerializedVaultKeyset* serialized) const;

  // Derives secrets and other values from User Passkey.
  //
  // Parameters
  //   passkey - The User Passkey, from which to derive the secrets.
  //   salt - The salt used when deriving the secrests.
  //   gen_secrets (OUT) - Vector containing resulting secrets.
  //
  // The blob in |gen_secrets| should be allocated by the caller.
  bool DeriveSecretsSCrypt(const brillo::Blob& passkey,
                           const brillo::Blob& salt,
                           std::vector<brillo::SecureBlob*> gen_secrets) const;

  // Converts the passkey to authorization data for a TPM-backed crypto token.
  //
  // Parameters
  //   passkey - The passkey from which to derive the authorization data.
  //   salt - The salt file used in deriving the authorization data.
  //   auth_data (OUT) - The token authorization data.
  virtual bool PasskeyToTokenAuthData(const brillo::Blob& passkey,
                                      const base::FilePath& salt_file,
                                      brillo::SecureBlob* auth_data) const;

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
                                const brillo::Blob& salt,
                                brillo::SecureBlob* passkey);

  // Ensures that the TPM is connected
  CryptoError EnsureTpm(bool reload_key) const;

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

  // Attempts to reset an LE credential, specified by |serialized_reset|
  // with an unencrypted key represented by |vk|.
  // Returns true on success.
  // On failure, false is returned and |error| is set with the appropriate
  // error.
  bool ResetLECredential(const SerializedVaultKeyset& serialized_reset,
                         CryptoError* error,
                         const VaultKeyset& vk) const;

  // Removes an LE credential specified by |label|.
  // Returns true on success, false otherwise.
  bool RemoveLECredential(uint64_t label) const;

  // Returns whether the provided label needs valid PCR criteria attached.
  bool NeedsPcrBinding(const uint64_t& label) const;

  // Sets whether or not to use the TPM (must be called before init, depends
  // on the presence of a functioning, initialized TPM).  The TPM is merely used
  // to add a layer of difficulty in a brute-force attack against the user's
  // credentials.
  void set_use_tpm(bool value) {
    use_tpm_ = value;
  }

  // Sets the TPM implementation
  void set_tpm(Tpm* value) {
    tpm_ = value;
  }

  // Gets whether the TPM is set
  bool has_tpm() {
    return (tpm_ != NULL);
  }

  // Gets the TPM implementation
  Tpm* get_tpm() {
    return tpm_;
  }

  // Checks if the cryptohome key is loaded in TPM
  bool is_cryptohome_key_loaded() const;

  // Sets the Platform implementation
  // Does NOT take ownership of the pointer.
  void set_platform(Platform* value) {
    platform_ = value;
  }

  Platform* platform() {
    return platform_;
  }

  void set_scrypt_max_encrypt_time(double max_time) {
    scrypt_max_encrypt_time_ = max_time;
  }

  void set_le_manager_for_testing(
      std::unique_ptr<LECredentialManager> le_manager) {
    le_manager_ = std::move(le_manager);
  }

  static const int64_t kSaltMax;

 private:
  // Converts a TPM error to a Crypto error
  CryptoError TpmErrorToCrypto(Tpm::TpmRetryAction retry_action) const;

  bool GenerateEncryptedRawKeyset(const VaultKeyset& vault_keyset,
                                  const brillo::SecureBlob& vkk_key,
                                  const brillo::SecureBlob& fek_iv,
                                  const brillo::SecureBlob& chaps_iv,
                                  brillo::SecureBlob* cipher_text,
                                  brillo::SecureBlob* wrapped_chaps_key) const;

  bool EncryptTPM(const VaultKeyset& vault_keyset,
                  const brillo::SecureBlob& key,
                  const brillo::SecureBlob& salt,
                  const std::string& obfuscated_username,
                  SerializedVaultKeyset* serialized) const;

  // Encrypt a provided blob using Scrypt encryption.
  //
  // This is a helper function used by EncryptScrypt() to encrypt various
  // data blobs. The parameters are as follows:
  // - blob: Data blob to be encrypted.
  // - key_source: User passphrase key used for encryption.
  // - wrapped_blob: Pointer to blob where encrypted data is stored.
  //
  // Returns true on success, and false on failure.
  bool EncryptScryptBlob(const brillo::SecureBlob& blob,
                         const brillo::SecureBlob& key_source,
                         brillo::SecureBlob* wrapped_blob) const;

  bool EncryptScrypt(const VaultKeyset& vault_keyset,
                     const brillo::SecureBlob& key,
                     SerializedVaultKeyset* serialized) const;

  bool EncryptLECredential(const VaultKeyset& vault_keyset,
                           const brillo::SecureBlob& key,
                           const brillo::SecureBlob& salt,
                           const std::string& obfuscated_username,
                           SerializedVaultKeyset* serialized) const;

  bool DecryptTPM(const SerializedVaultKeyset& serialized,
                  const brillo::SecureBlob& key,
                  bool is_pcr_extended,
                  CryptoError* error,
                  VaultKeyset* vault_keyset) const;

  // Companion decryption function for Crypto::EncryptScryptBlob()
  // This is a helper function used by DecryptScrypt() to decrypt
  // the data blobs which were encrypted using EncryptScryptBlob().
  //
  // Returns true on success. On failure, false is returned, and
  // |error| is set with the appropriate error code.
  bool DecryptScryptBlob(const brillo::SecureBlob& wrapped_blob,
                         const brillo::SecureBlob& key,
                         brillo::SecureBlob* blob,
                         CryptoError* error) const;

  bool DecryptScrypt(const SerializedVaultKeyset& serialized,
                     const brillo::SecureBlob& key,
                     CryptoError* error,
                     VaultKeyset* keyset) const;

  bool DecryptLECredential(const SerializedVaultKeyset& serialized,
                           const brillo::SecureBlob& key,
                           CryptoError* error,
                           VaultKeyset* vault_keyset) const;

  bool EncryptAuthorizationData(SerializedVaultKeyset* serialized,
                                const brillo::SecureBlob& vkk_key,
                                const brillo::SecureBlob& vkk_iv) const;

  void DecryptAuthorizationData(const SerializedVaultKeyset& serialized,
                                VaultKeyset* keyset,
                                const brillo::SecureBlob& vkk_key,
                                const brillo::SecureBlob& vkk_iv) const;

  bool IsTPMPubkeyHash(const std::string& hash, CryptoError* error) const;

  // Computes the PCR digest for default state as well as the future digest
  // obtained if PCR for ARC++ would be extended with |obfuscated_username|
  // value. Populates the results in |valid_pcr_criteria| where each pair
  // represents the bitmask of used PCR indexes and the expected digest.
  bool GetValidPCRValues(const std::string& obfuscated_username,
                         ValidPcrCriteria* valid_pcr_criteria) const;

  // Returns the map with expected PCR values for the user.
  std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const;

  // Returns the tpm_key data taken from |serialized|, specifically if the
  // keyset is PCR_BOUND and |is_pcr_extended| the data is taken from
  // extended_tpm_key. Otherwise the data from tpm_key is used.
  brillo::SecureBlob GetTpmKeyFromSerialized(
      const SerializedVaultKeyset& serialized,
      bool is_pcr_extended) const;

  // Decrypt the |vault_key| that is not bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmNotBoundToPcr(const SerializedVaultKeyset& serialized,
                               const brillo::SecureBlob& vault_key,
                               const brillo::SecureBlob& tpm_key,
                               const brillo::SecureBlob& salt,
                               CryptoError* error,
                               brillo::SecureBlob* vkk_iv,
                               brillo::SecureBlob* vkk_key) const;

  // Decrypt the |vault_key| that is bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmBoundToPcr(const brillo::SecureBlob& vault_key,
                            const brillo::SecureBlob& tpm_key,
                            const brillo::SecureBlob& salt,
                            CryptoError* error,
                            brillo::SecureBlob* vkk_iv,
                            brillo::SecureBlob* vkk_key) const;

  // If set, the TPM will be used during the encryption of the vault keyset
  bool use_tpm_;

  // The TPM implementation
  Tpm* tpm_;

  // Platform abstraction
  Platform* platform_;

  // The TpmInit object used to reload Cryptohome key
  TpmInit* tpm_init_;

  double scrypt_max_encrypt_time_;

  // Handler for Low Entropy credentials.
  std::unique_ptr<LECredentialManager> le_manager_;

  DISALLOW_COPY_AND_ASSIGN(Crypto);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_H_
