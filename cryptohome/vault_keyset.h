// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_VAULT_KEYSET_H_
#define CRYPTOHOME_VAULT_KEYSET_H_

#include <optional>
#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

// VaultKeyset holds the File Encryption Key (FEK) and File Name Encryption Key
// (FNEK) and their corresponding signatures.
class VaultKeyset {
 public:
  // Constructors and destructors.
  VaultKeyset() = default;
  VaultKeyset(VaultKeyset&&) = default;
  VaultKeyset(const VaultKeyset&) = default;
  VaultKeyset& operator=(const VaultKeyset&) = default;

  // Does not take ownership of platform and crypto. The objects pointed to by
  // them must outlive this object.
  void Initialize(libstorage::Platform* platform, Crypto* crypto);

  // This function initializes the VaultKeyset as a backup keyset by setting the
  // |backup_vk_| field to true. Does not take ownership of platform and crypto.
  // The objects pointed to by them must outlive this object.
  void InitializeAsBackup(libstorage::Platform* platform, Crypto* crypto);

  // Populates the fields from a SerializedVaultKeyset.
  void InitializeFromSerialized(const SerializedVaultKeyset& serialized);

  // Populates the fields from a Vaultkeyset to add a new key for the user.
  void InitializeToAdd(const VaultKeyset& vault_keyset);

  // The following methods deal with importing another object type into this
  // VaultKeyset container.
  [[nodiscard]] bool FromKeysBlob(const brillo::SecureBlob& keys_blob);

  // The following two methods export this VaultKeyset container to other
  // objects.
  [[nodiscard]] bool ToKeysBlob(brillo::SecureBlob* keys_blob) const;

  // Do not call Load directly, use KeysetManagement::LoadVaultKeysetForUser.
  [[nodiscard]] bool Load(const base::FilePath& filename);

  // Encrypt must be called first.
  bool Save(const base::FilePath& filename);

  // Load must be called first.
  // Decrypts the encrypted fields of the VaultKeyset from serialized with the
  // provided |key_blobs|.
  CryptoStatus DecryptEx(const KeyBlobs& key_blobs);

  // Encrypts the VaultKeyset fields with the provided |key_blobs| based on the
  // encryption mechanisms provided by the |auth_state|.
  CryptohomeStatus EncryptEx(const KeyBlobs& key_blobs,
                             const AuthBlockState& auth_state);

  // Convenience methods to initialize a new VaultKeyset with random values.
  void CreateRandomChapsKey();
  void CreateRandomResetSeed();
  void CreateFromFileSystemKeyset(const FileSystemKeyset& file_system_keyset);

  // Construct a filesystem keyset based on the contents of this vault.
  FileSystemKeyset ToFileSystemKeyset() const;

  // Methods to access runtime class state.
  const base::FilePath& GetSourceFile() const;

  void SetAuthLocked(bool locked);
  bool GetAuthLocked() const;

  // Group 1. Methods to access plaintext metadata as stored in AuthBlockState.
  // Returns the SerializedVaultKeyset flags.
  int32_t GetFlags() const;
  void SetFlags(int32_t flags);

  // Getters and setters for the index. See the |legacy_index_| member for a
  // comment explaining the legacy name.
  void SetLegacyIndex(int index);
  const int GetLegacyIndex() const;

  bool HasTpmPublicKeyHash() const;
  const brillo::Blob& GetTpmPublicKeyHash() const;
  void SetTpmPublicKeyHash(const brillo::Blob& hash);

  bool HasPasswordRounds() const;
  int32_t GetPasswordRounds() const;

  bool HasKeyData() const;
  void SetKeyData(const KeyData& key_data);
  void ClearKeyData();
  const KeyData& GetKeyData() const;

  // Gets the KeyData or return default value if it's empty.
  KeyData GetKeyDataOrDefault() const;

  // Gets the label from the KeyData.
  std::string GetLabel() const;

  // Checks the key data policy for low entropy credential (not the flags).
  bool IsLECredential() const;

  // Populates the le cred policy field in |key_data_|. |key_data_| is created
  // if empty. An LE credential is a PinWeaver credential.
  void SetLowEntropyCredential(bool is_le_cred);

  // Sets the label on |key_data_|. |key_data_| is created if empty.
  void SetKeyDataLabel(const std::string& key_label);

  void SetResetIV(const brillo::Blob& iv);
  const brillo::Blob& GetResetIV() const;

  void SetLELabel(uint64_t label);
  bool HasLELabel() const;
  uint64_t GetLELabel() const;

  void SetResetSalt(const brillo::Blob& reset_salt);
  bool HasResetSalt() const;
  const brillo::Blob& GetResetSalt() const;

  void SetFSCryptPolicyVersion(int32_t policy_version);
  int32_t GetFSCryptPolicyVersion() const;

  bool HasVkkIv() const;
  const brillo::Blob& GetVkkIv() const;

  // Group 2. Fields containing wrapped data.

  void SetWrappedKeyset(const brillo::Blob& wrapped_keyset);
  const brillo::Blob& GetWrappedKeyset() const;

  bool HasWrappedChapsKey() const;
  void SetWrappedChapsKey(const brillo::Blob& wrapped_chaps_key);
  const brillo::Blob& GetWrappedChapsKey() const;
  void ClearWrappedChapsKey();

  bool HasTPMKey() const;
  void SetTPMKey(const brillo::Blob& tpm_key);
  const brillo::Blob& GetTPMKey() const;

  bool HasExtendedTPMKey() const;
  void SetExtendedTPMKey(const brillo::Blob& tpm_key);
  const brillo::Blob& GetExtendedTPMKey() const;

  bool HasWrappedResetSeed() const;
  void SetWrappedResetSeed(const brillo::Blob& reset_seed);
  const brillo::Blob& GetWrappedResetSeed() const;

  bool HasSignatureChallengeInfo() const;
  const SerializedVaultKeyset::SignatureChallengeInfo&
  GetSignatureChallengeInfo() const;
  void SetSignatureChallengeInfo(
      const SerializedVaultKeyset::SignatureChallengeInfo& info);

  // Group 3. Unwrapped data.

  const brillo::SecureBlob& GetFek() const;
  const brillo::SecureBlob& GetFekSig() const;
  const brillo::SecureBlob& GetFekSalt() const;
  const brillo::SecureBlob& GetFnek() const;
  const brillo::SecureBlob& GetFnekSig() const;
  const brillo::SecureBlob& GetFnekSalt() const;

  void SetChapsKey(const brillo::SecureBlob& chaps_key);
  const brillo::SecureBlob& GetChapsKey() const;

  void SetResetSeed(const brillo::SecureBlob& reset_seed);
  const brillo::SecureBlob& GetResetSeed() const;

  void SetResetSecret(const brillo::SecureBlob& reset_secret);
  const brillo::SecureBlob& GetResetSecret() const;

  // This populates each sub type of AuthBlockState into the caller allocated
  // object.
  [[nodiscard]] bool GetTpmBoundToPcrState(AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetTpmNotBoundToPcrState(AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetPinWeaverState(AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetSignatureChallengeState(
      AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetScryptState(AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetDoubleWrappedCompatState(
      AuthBlockState* auth_state) const;
  [[nodiscard]] bool GetTpmEccState(AuthBlockState* auth_state) const;

  // Reads an auth block state and update the VaultKeyset with what it
  // returns.
  void SetAuthBlockState(const AuthBlockState& auth_state);

  // Set each type of AuthBlockState's sub messages.
  void SetTpmNotBoundToPcrState(
      const TpmNotBoundToPcrAuthBlockState& auth_state);
  void SetTpmBoundToPcrState(const TpmBoundToPcrAuthBlockState& auth_state);
  void SetPinWeaverState(const PinWeaverAuthBlockState& auth_state);
  void SetScryptState(const ScryptAuthBlockState& auth_state);
  void SetChallengeCredentialState(
      const ChallengeCredentialAuthBlockState& auth_state);
  void SetTpmEccState(const TpmEccAuthBlockState& auth_state);

  // Returns whether the VaultKeyset is setup for backup purpose.
  bool IsForBackup() const { return backup_vk_; }
  // Returns whether the VaultKeyset is migrated to USS.
  bool IsMigrated() const { return migrated_vk_; }

  // Setter for the |backup_vk_|.
  void set_backup_vk_for_testing(bool value) { backup_vk_ = value; }

  // Setter for the |migrated_vk_|.
  void set_migrated_vk_for_testing(bool value) { migrated_vk_ = value; }

  // Marks the VaultKeyset migrated. Every migrated VaultKeyset to USS should be
  // set as a backup VaultKeyset for USS.
  void MarkMigrated(bool migrated);

 private:
  // Converts the class to a protobuf for serialization to disk.
  SerializedVaultKeyset ToSerialized() const;

  // Clears all the fields set from the SerializedVaultKeyset.
  void ResetVaultKeyset();

  // This function decrypts a keyset that is encrypted with a VaultKeysetKey.
  //
  // Parameters
  //   serialized - The serialized vault keyset protobuf.
  //   vkk_data - Key data includes the VaultKeysetKey to decrypt the serialized
  // keyset.
  // Return
  //   error - The specific error code on failure.
  CryptoStatus UnwrapVKKVaultKeyset(const SerializedVaultKeyset& serialized,
                                    const KeyBlobs& vkk_data);

  // This function decrypts a keyset that is encrypted with an scrypt derived
  // key.
  //
  // Parameters
  //   serialized - The serialized vault keyset protobuf.
  //   vkk_data - Key data that includes the scrypt derived keys.
  // Return
  //   error - The specific error code on failure.
  CryptoStatus UnwrapScryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                       const KeyBlobs& vkk_data);

  // This function encrypts a keyset with a VaultKeysetKey.
  //
  // Parameters
  //   key_blobs - Key bloc that stores VaultKeysetKey.
  CryptohomeStatus WrapVaultKeysetWithAesDeprecated(const KeyBlobs& blobs);

  // This function encrypts a VaultKeyset with an scrypt derived key.
  //
  // Parameters
  //   auth_block_state - AuthBlockState that stores salts for scrypt wrapping.
  //   key_blobs - Key blob that stores scrypt derived keys.
  // Return
  //   error - The specific error code on failure.
  CryptohomeStatus WrapScryptVaultKeyset(const AuthBlockState& auth_block_state,
                                         const KeyBlobs& key_blobs);

  // This function consumes the Vault Keyset Key (VKK) and IV, and produces
  // the unwrapped secrets from the Vault Keyset.
  //
  // Parameters
  //   serialized - The serialized vault keyset protobuf.
  //   vkk_data - The VKK and the VKK IV.
  // Return
  //   error - The specific error code on failure.
  CryptoStatus UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                                 const KeyBlobs& vkk_data);

  // Decrypts an encrypted vault keyset which is obtained from the unwrapped
  // secrets returned from UnwrapVaultKeyset() using the key_blobs.
  //
  // Parameters
  //   key_blobs - KeyBlobs to decrypt serialized VaultKeyset.
  // Return
  //   error - The specific error code on failure.
  CryptoStatus DecryptVaultKeysetEx(const KeyBlobs& key_blobs);

  // These store run time state for the class.
  libstorage::Platform* platform_ = nullptr;
  Crypto* crypto_ = nullptr;
  bool loaded_ = false;
  bool encrypted_ = false;
  base::FilePath source_file_;

  // The following data members are grouped into three categories. Each category
  // should be split into a separate object in the future.

  // Group 1. AuthBlockState. This is metadata used to derive the keys,
  // persisted as plaintext.
  int32_t flags_ = 0;
  // Field to tag the VaultKeyset as a backup VaultKeyset for USS.
  bool backup_vk_ = false;
  // Field to tag the VaultKeyset as a migrated VaultKeyset to USS.
  bool migrated_vk_ = false;
  // The salt used to derive the user input in auth block.
  brillo::Blob auth_salt_;
  // The IV used to encrypt the encryption key.
  std::optional<brillo::Blob> vkk_iv_;
  // legacy_index_ is the index of the keyset for the user. It is called legacy
  // due to previous plans to fully switch to label-based addressing, which,
  // unfortunately, wasn't followed through.
  // TODO(dlunev): rename it not to say legacy.
  int legacy_index_ = -1;
  bool auth_locked_ = false;
  // This is used by the TPM AuthBlocks to make sure the keyset was sealed to
  // the TPM on this system. It's not a security check, but a diagnostic.
  std::optional<brillo::Blob> tpm_public_key_hash_;
  // Passwords which are TPM backed, not PCR bound, and not run through scrypt
  // before the TPM operation, have a number of rounds to run the key derivation
  // function.
  std::optional<int32_t> password_rounds_;
  // Plaintet metadata describing the key.
  std::optional<KeyData> key_data_;
  // Used for the reset seed wrapping.
  std::optional<brillo::Blob> reset_iv_;
  // The label for PinWeaver secrets.
  std::optional<uint64_t> le_label_;
  // IV for the file encryption key of PinWeaver credentials.
  std::optional<brillo::Blob> le_fek_iv_;
  // IV for the chaps key wrapping of PinWeaver credentials.
  std::optional<brillo::Blob> le_chaps_iv_;
  // Used with the resed seed to derive the reset secret. PinWeaver only.
  std::optional<brillo::Blob> reset_salt_;
  // Specifies which version of fscrypt encryption policy this is used with.
  std::optional<int32_t> fscrypt_policy_version_;

  // Group 2. Wrapped stuff.
  // An encrypted copy of the VaultKeysetKeys struct, which holds important
  // fields such as a the file encryption key.
  brillo::Blob wrapped_keyset_;
  // Wrapped copy of the key used to authenticate with the PKCS#11 service.
  std::optional<brillo::Blob> wrapped_chaps_key_;
  // The VaultKeysetKey encrypted with the user's password and TPM.
  std::optional<brillo::Blob> tpm_key_;
  // Used by the PCR bound AuthBlock where the TPM's PCR is extended with the
  // username.
  std::optional<brillo::Blob> extended_tpm_key_;
  // The wrapped reset seed for LE credentials.
  std::optional<brillo::Blob> wrapped_reset_seed_;
  // Information specific to the signature-challenge response protection. This
  // has plaintext metadata in it, but also the sealed secret, so it goes here.
  std::optional<SerializedVaultKeyset::SignatureChallengeInfo>
      signature_challenge_info_;

  // Group 3. Unwrapped secrets.
  // The file encryption key present in all VaultKeysets.
  brillo::SecureBlob fek_;
  // Randomly generated key identifier.
  brillo::SecureBlob fek_sig_;
  // Randomly generated salt for use with the file encryption key.
  brillo::SecureBlob fek_salt_;
  // The file name encryption key present in dircrypto, not fscrypt keysets.
  brillo::SecureBlob fnek_;
  // Randomly generated key identifier for the |fnek_|.
  brillo::SecureBlob fnek_sig_;
  // Randomly generated salt for use with the file name encryption key.
  brillo::SecureBlob fnek_salt_;
  // Unwrapped key used for PKCS#11 operations.
  brillo::SecureBlob chaps_key_;
  // The seed mixed with the salt to derive the reset secret.
  brillo::SecureBlob reset_seed_;
  // Used by LECredentials only.
  brillo::SecureBlob reset_secret_;

  // With the SerializedVaultKeyset properly abstracted by VaultKeyset, Crypto
  // should really be folded into VaultKeyset class. But this amount of
  // refactoring for legacy code is undesirable, so it is made a friend class.
  friend class Crypto;

  FRIEND_TEST_ALL_PREFIXES(CryptoTest, TpmStepTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, Tpm1_2_StepTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, TpmDecryptFailureTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, ScryptStepTest);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, Decrypt);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, Encrypt);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, DecryptWithKeyBlobs);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, EncryptWithKeyBlobs);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, EncryptFail);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, EncryptTestReset);
  FRIEND_TEST_ALL_PREFIXES(VaultKeysetTest, GetEccAuthBlockStateTest);
  FRIEND_TEST_ALL_PREFIXES(VaultKeysetTest, EncryptionTest);
  FRIEND_TEST_ALL_PREFIXES(VaultKeysetTest, DecryptionTest);
  FRIEND_TEST_ALL_PREFIXES(VaultKeysetTest, LibScryptBackwardCompatibility);
  FRIEND_TEST_ALL_PREFIXES(KeysetManagementTest, AddInitialKeyset);
  FRIEND_TEST_ALL_PREFIXES(KeysetManagementTest, AddResetSeed);
  FRIEND_TEST_ALL_PREFIXES(KeysetManagementTest, AddWrappedResetSeed);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_VAULT_KEYSET_H_
