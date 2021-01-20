// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_VAULT_KEYSET_H_
#define CRYPTOHOME_VAULT_KEYSET_H_

#include <string>

#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/macros.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state.pb.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class Crypto;
class Platform;

// VaultKeyset holds the File Encryption Key (FEK) and File Name Encryption Key
// (FNEK) and their corresponding signatures.
class VaultKeyset {
 public:
  // Constructors and destructors.
  VaultKeyset();
  VaultKeyset(VaultKeyset&&) = default;
  VaultKeyset(const VaultKeyset&) = default;
  VaultKeyset& operator=(const VaultKeyset&) = default;
  virtual ~VaultKeyset();

  // Does not take ownership of platform and crypto. The objects pointed to by
  // them must outlive this object.
  virtual void Initialize(Platform* platform, Crypto* crypto);

  //  The following methods deal with importing another object type into this
  //  VaultKeyset container.
  virtual void FromKeys(const VaultKeysetKeys& keys);
  virtual bool FromKeysBlob(const brillo::SecureBlob& keys_blob);

  // The following two methods export this VaultKeyset container to other
  // objects.
  virtual bool ToKeys(VaultKeysetKeys* keys) const;
  virtual bool ToKeysBlob(brillo::SecureBlob* keys_blob) const;

  // Do not call Load directly, use KeysetManagement::LoadVaultKeysetForUser.
  virtual bool Load(const base::FilePath& filename);

  // Encrypt must be called first.
  virtual bool Save(const base::FilePath& filename);

  // Load must be called first. |crypto_error| may be null.
  virtual bool Decrypt(const brillo::SecureBlob& key,
                       bool is_pcr_extended,
                       CryptoError* crypto_error);

  virtual bool Encrypt(const brillo::SecureBlob& key,
                       const std::string& obfuscated_username);

  // Convenience methods to initialize a new VaultKeyset with random values.
  virtual void CreateRandomChapsKey();
  virtual void CreateRandomResetSeed();
  virtual void CreateRandom();

  // Methods to access runtime class state.
  virtual const base::FilePath& GetSourceFile() const;

  virtual void SetAuthLocked(bool locked);
  virtual bool GetAuthLocked() const;

  // Group 1. Methods to access plaintext metadata as stored in AuthBlockState.
  // Returns the SerializedVaultKeyset flags.
  virtual int32_t GetFlags() const;
  virtual void SetFlags(int32_t flags);

  // Returns the salt from the SerializedVaultKeyset protobuf on disk.
  virtual const brillo::SecureBlob& GetSalt() const;

  // Getters and setters for the index. See the |legacy_index_| member for a
  // comment explaining the legacy name.
  virtual void SetLegacyIndex(int index);
  virtual const int GetLegacyIndex() const;

  virtual bool HasTpmPublicKeyHash() const;
  virtual const brillo::SecureBlob& GetTpmPublicKeyHash() const;
  virtual void SetTpmPublicKeyHash(const brillo::SecureBlob& hash);

  virtual bool HasPasswordRounds() const;
  virtual int32_t GetPasswordRounds() const;

  virtual bool HasLastActivityTimestamp() const;
  virtual int64_t GetLastActivityTimestamp() const;

  virtual bool HasKeyData() const;
  virtual void SetKeyData(const KeyData& key_data);
  virtual void ClearKeyData();
  virtual const KeyData& GetKeyData() const;

  // Gets the label from the KeyData.
  virtual std::string GetLabel() const;

  virtual int GetFscryptPolicyVersion();
  virtual void SetFscryptPolicyVersion(int policy_version);

  // Checks the key data policy for low entropy credential (not the flags).
  virtual bool IsLECredential() const;

  // Populates the le cred policy field in |key_data_|. |key_data_| is created
  // if empty. An LE credential is a PinWeaver credential.
  virtual void SetLowEntropyCredential(bool is_le_cred);

  // Checks the flags field if this is a signature challenge credential.
  virtual bool IsSignatureChallengeProtected() const;

  // Sets the label on |key_data_|. |key_data_| is created if empty.
  virtual void SetKeyDataLabel(const std::string& key_label);

  virtual void SetResetIV(const brillo::SecureBlob& iv);
  virtual bool HasResetIV() const;
  virtual const brillo::SecureBlob& GetResetIV() const;

  virtual void SetLELabel(uint64_t label);
  virtual bool HasLELabel() const;
  virtual uint64_t GetLELabel() const;

  virtual void SetLEFekIV(const brillo::SecureBlob& iv);
  virtual bool HasLEFekIV() const;
  virtual const brillo::SecureBlob& GetLEFekIV() const;

  virtual void SetLEChapsIV(const brillo::SecureBlob& iv);
  virtual bool HasLEChapsIV() const;
  virtual const brillo::SecureBlob& GetLEChapsIV() const;

  virtual void SetResetSalt(const brillo::SecureBlob& reset_salt);
  virtual bool HasResetSalt() const;
  virtual const brillo::SecureBlob& GetResetSalt() const;

  virtual void SetFSCryptPolicyVersion(int32_t policy_version);
  virtual bool HasFSCryptPolicyVersion() const;
  virtual int32_t GetFSCryptPolicyVersion() const;

  // Group 2. Fields containing wrapped data.

  virtual void SetWrappedKeyset(const brillo::SecureBlob& wrapped_keyset);
  virtual const brillo::SecureBlob& GetWrappedKeyset() const;

  virtual bool HasWrappedChapsKey() const;
  virtual void SetWrappedChapsKey(const brillo::SecureBlob& wrapped_chaps_key);
  virtual const brillo::SecureBlob& GetWrappedChapsKey() const;
  virtual void ClearWrappedChapsKey();

  virtual bool HasTPMKey() const;
  virtual void SetTPMKey(const brillo::SecureBlob& tpm_key);
  virtual const brillo::SecureBlob& GetTPMKey() const;

  virtual bool HasExtendedTPMKey() const;
  virtual void SetExtendedTPMKey(const brillo::SecureBlob& tpm_key);
  virtual const brillo::SecureBlob& GetExtendedTPMKey() const;

  virtual bool HasWrappedResetSeed() const;
  virtual void SetWrappedResetSeed(const brillo::SecureBlob& reset_seed);
  virtual const brillo::SecureBlob& GetWrappedResetSeed() const;

  virtual bool HasSignatureChallengeInfo() const;
  virtual const SerializedVaultKeyset::SignatureChallengeInfo&
  GetSignatureChallengeInfo() const;
  virtual void SetSignatureChallengeInfo(
      const SerializedVaultKeyset::SignatureChallengeInfo& info);

  // Group 3. Unwrapped data.

  virtual const brillo::SecureBlob& GetFek() const;
  virtual const brillo::SecureBlob& GetFekSig() const;
  virtual const brillo::SecureBlob& GetFekSalt() const;
  virtual const brillo::SecureBlob& GetFnek() const;
  virtual const brillo::SecureBlob& GetFnekSig() const;
  virtual const brillo::SecureBlob& GetFnekSalt() const;

  virtual void SetChapsKey(const brillo::SecureBlob& chaps_key);
  virtual const brillo::SecureBlob& GetChapsKey() const;
  virtual void ClearChapsKey();

  virtual void SetResetSeed(const brillo::SecureBlob& reset_seed);
  virtual const brillo::SecureBlob& GetResetSeed() const;

  virtual void SetResetSecret(const brillo::SecureBlob& reset_secret);
  virtual const brillo::SecureBlob& GetResetSecret() const;

 private:
  // Converts the class to a protobuf for serialization to disk.
  SerializedVaultKeyset ToSerialized() const;

  // Clears all the fields set from the SerializedVaultKeyset.
  void ResetVaultKeyset();

  // Populates the fields from a SerializedVaultKeyset.
  void InitializeFromSerialized(const SerializedVaultKeyset& serialized);

  // Sets the wrapped keys and IVs.
  void SetWrappedKeyMaterial(const WrappedKeyMaterial& key_material);

  // Reads an auth block state and update the VaultKeyset with what it
  // returns.
  void SetAuthBlockState(const AuthBlockState& auth_state);

  // Set each type of AuthBlockState's sub messages.
  void SetTpmNotBoundToPcrState(
      const AuthBlockState::TpmNotBoundToPcrAuthBlockState& auth_state);
  void SetTpmBoundToPcrState(
      const AuthBlockState::TpmBoundToPcrAuthBlockState& auth_state);
  void SetPinWeaverState(
      const AuthBlockState::PinWeaverAuthBlockState& auth_state);
  void SetLibScryptCompatState(
      const AuthBlockState::LibScryptCompatAuthBlockState& auth_state);
  void SetChallengeCredentialState(
      const AuthBlockState::ChallengeCredentialAuthBlockState& auth_state);

  // These store run time state for the class.
  Platform* platform_;
  Crypto* crypto_;
  bool loaded_;
  bool encrypted_;
  base::FilePath source_file_;

  // The following data members are grouped into three categories. Each category
  // should be split into a separate object in the future.

  // Group 1. AuthBlockState. This is metadata used to derive the keys,
  // persisted as plaintext.
  int32_t flags_;
  // The salt used to derive the encryption key.
  brillo::SecureBlob salt_;
  // legacy_index_ is the index of the keyset for the user. It is called legacy
  // due to previous plans to fully switch to label-based addressing, which,
  // unfortunately, wasn't followed through.
  // TODO(dlunev): rename it not to say legacy.
  int legacy_index_;
  bool auth_locked_;
  // This is used by the TPM AuthBlocks to make sure the keyset was sealed to
  // the TPM on this system. It's not a security check, but a diagnostic.
  base::Optional<brillo::SecureBlob> tpm_public_key_hash_;
  // Passwords which are TPM backed, not PCR bound, and not run through scrypt
  // before the TPM operation, have a number of rounds to run the key derivation
  // function.
  base::Optional<int32_t> password_rounds_;
  // An optional timestamp field.
  base::Optional<int64_t> last_activity_timestamp_;
  // Plaintet metadata describing the key.
  base::Optional<KeyData> key_data_;
  // Used for the reset seed wrapping.
  base::Optional<brillo::SecureBlob> reset_iv_;
  // The label for PinWeaver secrets.
  base::Optional<uint64_t> le_label_;
  // IV for the file encryption key of PinWeaver credentials.
  base::Optional<brillo::SecureBlob> le_fek_iv_;
  // IV for the chaps key wrapping of PinWeaver credentials.
  base::Optional<brillo::SecureBlob> le_chaps_iv_;
  // Used with the resed seed to derive the reset secret. PinWeaver only.
  base::Optional<brillo::SecureBlob> reset_salt_;
  // Specifies which version of fscrypt encryption policy this is used with.
  base::Optional<int32_t> fscrypt_policy_version_;

  // Group 2. Wrapped stuff.
  // An encrypted copy of the VaultKeysetKeys struct, which holds important
  // fields such as a the file encryption key.
  brillo::SecureBlob wrapped_keyset_;
  // Wrapped copy of the key used to authenticate with the PKCS#11 service.
  base::Optional<brillo::SecureBlob> wrapped_chaps_key_;
  // The VaultKeysetKey encrypted with the user's password and TPM.
  base::Optional<brillo::SecureBlob> tpm_key_;
  // Used by the PCR bound AuthBlock where the TPM's PCR is extended with the
  // username.
  base::Optional<brillo::SecureBlob> extended_tpm_key_;
  // The reset seed for LE credentials.
  base::Optional<brillo::SecureBlob> wrapped_reset_seed_;
  // Information specific to the signature-challenge response protection. This
  // has plaintext metadata in it, but also the sealed secret, so it goes here.
  base::Optional<SerializedVaultKeyset::SignatureChallengeInfo>
      signature_challenge_info_;

  // Group 3. Unwrapped secrets.
  // TODO(kerrnel): Make these base::Optional<>
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

  FRIEND_TEST_ALL_PREFIXES(CryptoTest, TpmStepTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, Tpm1_2_StepTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, TpmDecryptFailureTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, DecryptionTest);
  FRIEND_TEST_ALL_PREFIXES(CryptoTest, ScryptStepTest);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, Encrypt);
  FRIEND_TEST_ALL_PREFIXES(LeCredentialsManagerTest, EncryptFail);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_VAULT_KEYSET_H_
