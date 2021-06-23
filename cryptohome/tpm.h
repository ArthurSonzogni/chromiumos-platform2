// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tpm - class for performing encryption/decryption in the TPM.  For cryptohome,
// the TPM may be used as a way to strengthen the security of the wrapped vault
// keys stored on disk.  When the TPM is enabled, there is a system-wide
// cryptohome RSA key that is used during the encryption/decryption of these
// keys.
// TODO(wad) make more functions virtual for use in mock_tpm.h.

#ifndef CRYPTOHOME_TPM_H_
#define CRYPTOHOME_TPM_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <brillo/secure_blob.h>
#include <libhwsec/error/tpm_error.h>
#include <openssl/rsa.h>

namespace cryptohome {

using TpmKeyHandle = uint32_t;

class LECredentialBackend;
class SignatureSealingBackend;
class Tpm;

constexpr uint32_t kNotBoundToPCR = UINT32_MAX;
constexpr uint32_t kTpmBootPCR = 0;

// The PCR index used to restrict the device to access to a single user data.
#if USE_TPM_DYNAMIC
constexpr uint32_t kTpmSingleUserPCR = 11;
#else
constexpr uint32_t kTpmSingleUserPCR = 4;
#endif

const char kDefaultPcrValue[32] = {0};

// Specifies what the key can be used for.
enum class AsymmetricKeyUsage { kDecryptKey, kSignKey, kDecryptAndSignKey };

// This class provides a wrapper around TpmKeyHandle, and manages freeing of
// TPM resources associated with TPM keys. It does not take ownership of the
// Tpm pointer provided.
class ScopedKeyHandle {
 public:
  ScopedKeyHandle();
  ScopedKeyHandle(const ScopedKeyHandle&) = delete;
  ScopedKeyHandle& operator=(const ScopedKeyHandle&) = delete;

  ~ScopedKeyHandle();
  TpmKeyHandle value();
  TpmKeyHandle release();
  bool has_value();
  void reset(Tpm* tpm, TpmKeyHandle handle);

 private:
  Tpm* tpm_;
  TpmKeyHandle handle_;
};

class Tpm {
 public:
  enum TpmVersion {
    TPM_UNKNOWN = 0,
    TPM_1_2 = 1,
    TPM_2_0 = 2,
  };

  enum TpmNvramFlags {
    // NVRAM space is write-once; lock by writing 0 bytes
    kTpmNvramWriteDefine = (1 << 0),

    // NVRAM space is only accessible if PCR0 has the same value it did
    // when the space was created
    kTpmNvramBindToPCR0 = (1 << 1),

    // NVRAM space is readable by firmware (PPREAD is set)
    kTpmNvramFirmwareReadable = (1 << 2),
  };

  // Dependencies on the tpm owner password. Each of the listed entities
  // clears its dependency when it no longer needs the owner password for
  // further initialization. The password is cleared by tpm_manager once
  // all dependencies are cleared.
  enum class TpmOwnerDependency {
    kInstallAttributes,
    kAttestation,
  };

  // Results of PCR quoting.
  enum class QuotePcrResult {
    kSuccess,
    kFailure,
    kInvalidPcrValue,
  };

  struct TpmStatusInfo {
    uint32_t last_tpm_error = 0;
    bool can_connect = false;
    bool can_load_srk = false;
    bool can_load_srk_public_key = false;
    bool srk_vulnerable_roca = false;
    bool has_cryptohome_key = false;
    bool can_encrypt = false;
    bool can_decrypt = false;
    bool this_instance_has_context = false;
    bool this_instance_has_key_handle = false;
  };

  // Holds TPM version info.
  struct TpmVersionInfo {
    uint32_t family;
    uint64_t spec_level;
    uint32_t manufacturer;
    uint32_t tpm_model;
    uint64_t firmware_version;
    std::string vendor_specific;

    // Computes a fingerprint of the version parameters in the struct fields by
    // running them through a hash function and truncating the output. The idea
    // is to produce a fingerprint that's unique in practice for each set of
    // real-life version parameters.
    int GetFingerprint() const;
  };

  // Holds status information pertaining to TPM firmware updates for Infineon
  // TPMs.
  struct IFXFieldUpgradeInfo {
    // Describes status of a firmware package.
    struct FirmwarePackage {
      uint32_t package_id;
      uint32_t version;
      uint32_t stale_version;
    };

    // Chunk size for transmitting the firmware update.
    uint16_t max_data_size;
    // Version numbers of the bootloader in ROM.
    FirmwarePackage bootloader;
    // Version numbers for the two writable firmware slots.
    FirmwarePackage firmware[2];
    // Status of the TPM - 0x5a3c indicates bootloader mode, i.e. no running
    // TPM firmware.
    uint16_t status;
    // Version numbers of the firmware for which installation has started, but
    // not completed.
    FirmwarePackage process_fw;
    // Total number of updates the TPM has installed in its entire lifetime.
    uint16_t field_upgrade_counter;
  };

  // Number of alerts supported by UMA
  static constexpr size_t kAlertsNumber = 45;
  struct AlertsData {
    // alert counters with UMA enum index
    uint16_t counters[kAlertsNumber];
  };

  // Constants for default labels for use with the CreateDelegate() method.
  static constexpr uint8_t kDefaultDelegateFamilyLabel = 1;
  static constexpr uint8_t kDefaultDelegateLabel = 2;

  static Tpm* GetSingleton();

  virtual ~Tpm() {}

  // Returns TPM version
  virtual TpmVersion GetVersion() = 0;

  // Encrypts a data blob using the provided RSA key. Returns a
  // hwsec::error::TPMErrorBase.
  //
  // Parameters
  //   key_handle - The loaded TPM key handle
  //   plaintext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   ciphertext (OUT) - Encrypted blob
  virtual hwsec::error::TPMErrorBase EncryptBlob(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& key,
      brillo::SecureBlob* ciphertext) = 0;

  // Decrypts a data blob using the provided RSA key. Returns a
  // hwsec::error::TPMErrorBase.
  //
  // Parameters
  //   key_handle - The loaded TPM key handle
  //   ciphertext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   pcr_map - The map of PCR index -> value bound to the key. This is used
  //             only for TPM 2.0. If the key is not bound to PCR, an empty map
  //             should be provided. For TPM 1.2 this parameter is ignored, even
  //             if the key is bound to PCR, so an empty map can be used.
  //   plaintext (OUT) - Decrypted blob
  virtual hwsec::error::TPMErrorBase DecryptBlob(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& ciphertext,
      const brillo::SecureBlob& key,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) = 0;

  // Seals a data blob to provided PCR data, while assigning a authorization
  // value derived from provided |auth_value|. Returns a
  // hwsec::error::TPMErrorBase.
  //
  // Parameters
  //   plaintext - The data blob to be sealed.
  //   auth_value - The blob to be used to derive the authorization value.
  //   pcr_map - The map of PCR index -> expected value when Unseal will be
  //             used.
  //   sealed_data (OUT) - Sealed blob.
  virtual hwsec::error::TPMErrorBase SealToPcrWithAuthorization(
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* sealed_data) = 0;

  // Preload the data for unsealing.
  // For TPM2.0, |sealed_data| is the data that needs to be load into the TPM.
  // For TPM1.2, the function has no effect.
  //
  // Parameters
  //   sealed_data - The sealed data.
  //   preload_handle (OUT) - A handle to the sealed data loaded into the TPM.
  virtual hwsec::error::TPMErrorBase PreloadSealedData(
      const brillo::SecureBlob& sealed_data,
      ScopedKeyHandle* preload_handle) = 0;

  // Unseal a data blob using the provided |auth_value| to derive the
  // authorization value. Also for TPM2.0, the session used to unseal is not
  // salted, meaning there's a security risk to leak the |auth_value|. That's
  // because it uses one expensive operation in decrypt to obtain the
  // auth_value, and we can't afford to add a second operation. This might
  // change in the future if we implement ECC encryption for salted sessions.
  // For TPM1.2 the |preload_handle| are unused. Returns a
  // hwsec::error::TPMErrorBase struct.
  //
  // Parameters
  //   preload_handle - The handle to the sealed data. (optional)
  //   sealed_data - The sealed data.
  //   auth_value - The blob used to derive the authorization value.
  //   pcr_map - The map of PCR index -> value bound to the key. This is used
  //             only for TPM 2.0. For TPM 1.2 this parameter is ignored, even
  //             so an empty map can be used.
  //   plaintext (OUT) - Unsealed blob.
  virtual hwsec::error::TPMErrorBase UnsealWithAuthorization(
      base::Optional<TpmKeyHandle> preload_handle,
      const brillo::SecureBlob& sealed_data,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) = 0;

  // Retrieves the sha1sum of the public key component of the RSA key
  virtual hwsec::error::TPMErrorBase GetPublicKeyHash(
      TpmKeyHandle key_handle, brillo::SecureBlob* hash) = 0;

  // Returns the owner password if this instance was used to take ownership.
  // This will only occur when the TPM is unowned, which will be on OOBE
  //
  // Parameters
  //   owner_password (OUT) - The random owner password used
  virtual bool GetOwnerPassword(brillo::SecureBlob* owner_password) = 0;

  // Returns whether or not the TPM is enabled.  This method call returns a
  // cached result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsEnabled() = 0;

  // Returns whether or not the TPM is owned.  This method call returns a cached
  // result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsOwned() = 0;

  // Returns whether or not the owner password is still retained.
  virtual bool IsOwnerPasswordPresent() = 0;

  // Returns whether or not the TPM has the permission to reset lock.
  virtual bool HasResetLockPermissions() = 0;

  // Returns whether or not the TPM is enabled and owned using a call to
  // Tspi_TPM_GetCapability.
  //
  // Unlike former functions, this function performs the check (which could take
  // some time) every time it is invoked. It does not use cached value.
  //
  // Parameters
  //   enabled (OUT) - Whether the TPM is enabled
  //   owned (OUT) - Whether the TPM is owned
  //
  // Returns true if the check was successfully carried out.
  virtual bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) = 0;

  // Gets random bytes from the TPM.
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomDataBlob(size_t length, brillo::Blob* data) = 0;

  // Gets random bytes from the TPM, returns them in a SecureBlob.
  // brillo::SecureBlob intentionally does not inherit from brillo::Blob.
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomDataSecureBlob(size_t length,
                                       brillo::SecureBlob* data) = 0;

  // Gets alerts data the TPM
  //
  // Parameters
  //   alerts (OUT) - Struct that contains TPM alerts information
  // Returns true is hardware supports Alerts reporting, false otherwise
  virtual bool GetAlertsData(Tpm::AlertsData* alerts) = 0;

  // Creates a NVRAM space in the TPM
  //
  // Parameters
  //   index - The index of the space
  //   length - The number of bytes to allocate
  //   flags - Flags for NVRAM space attributes; zero or more TpmNvramFlags
  // Returns false if the index or length invalid or the required
  // authorization is not possible.
  virtual bool DefineNvram(uint32_t index, size_t length, uint32_t flags) = 0;

  // Destroys a defined NVRAM space
  //
  // Parameters
  //  index - The index of the space to destroy
  // Returns false if the index is invalid or the required authorization
  // is not possible.
  virtual bool DestroyNvram(uint32_t index) = 0;

  // Writes the given blob to NVRAM
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to write (size==0 may be used for locking)
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool WriteNvram(uint32_t index, const brillo::SecureBlob& blob) = 0;

  // Writes the given blob to NVRAM with owner authorization
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to write (size==0 may be used for locking)
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool OwnerWriteNvram(uint32_t index,
                               const brillo::SecureBlob& blob) = 0;

  // Reads from the NVRAM index to the given blob
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to read
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool ReadNvram(uint32_t index, brillo::SecureBlob* blob) = 0;

  // Determines if the given index is defined in the TPM
  //
  // Parameters
  //  index - The index of the space
  // Returns true if it exists and false if it doesn't or there is a failure to
  // communicate with the TPM.
  virtual bool IsNvramDefined(uint32_t index) = 0;

  // Determines if the NVRAM space at the given index is bWriteDefine locked
  //
  // Parameters
  //   index - The index of the space
  // Returns true if locked and false if it is unlocked, the space does not
  // exist, or there is a TPM-related error.
  virtual bool IsNvramLocked(uint32_t index) = 0;

  // Locks NVRAM space for writing
  //
  // Parameters
  //  index - The index of the space
  // Returns true if the index has been successfully write-locked, and false
  // otherwise.
  virtual bool WriteLockNvram(uint32_t index) = 0;

  // Returns the reported size of the NVRAM space indicated by its index
  //
  // Parameters
  //   index - The index of the space
  // Returns the size of the space. If undefined or an error occurs, 0 is
  // returned.
  virtual unsigned int GetNvramSize(uint32_t index) = 0;

  // Seals a secret to PCR0 with the SRK.
  //
  // Parameters
  //   value - The value to be sealed.
  //   sealed_value - The sealed value.
  //
  // Returns true on success.
  virtual bool SealToPCR0(const brillo::SecureBlob& value,
                          brillo::SecureBlob* sealed_value) = 0;

  // Unseals a secret previously sealed with the SRK.
  //
  // Parameters
  //   sealed_value - The sealed value.
  //   value - The original value.
  //
  // Returns true on success.
  virtual bool Unseal(const brillo::SecureBlob& sealed_value,
                      brillo::SecureBlob* value) = 0;

  // Creates a TPM owner delegate for future use.
  //
  // Parameters
  //   bound_pcrs - Specifies the PCRs to which the delegate is bound.
  //   delegate_family_label - Specifies the label of the created delegate
  //                           family. Should be equal to
  //                           |kDefaultDelegateFamilyLabel| in most cases. Non-
  //                           default values are primarily intended for testing
  //                           purposes.
  //   delegate_label - Specifies the label of the created delegate. Should be
  //                    equal to |kDefaultDelegateLabel| in most cases. Non-
  //                    default values are primarily intended for testing
  //                    purposes.
  //   delegate_blob - The blob for the owner delegation.
  //   delegate_secret - The delegate secret that will be required to perform
  //                     privileged operations in the future.
  virtual bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                              uint8_t delegate_family_label,
                              uint8_t delegate_label,
                              brillo::Blob* delegate_blob,
                              brillo::Blob* delegate_secret) = 0;

  // Signs data using the TPM_SS_RSASSAPKCS1v15_DER scheme.  This method will
  // work with any signing key that has been assigned this scheme.  This
  // includes all keys created using CreateCertifiedKey.
  //
  // Parameters
  //   key_blob - An SRK-wrapped private key blob.
  //   input - The value to be signed.
  //   bound_pcr_index - If the signing key used is a PCR bound key, this arg
  //                     is the pcr to which it was bound. Else it is
  //                     kNotBoundToPCR.
  //   signature - On success, will be populated with the signature.
  virtual bool Sign(const brillo::SecureBlob& key_blob,
                    const brillo::SecureBlob& input,
                    uint32_t bound_pcr_index,
                    brillo::SecureBlob* signature) = 0;

  // Creates an SRK-wrapped key that has both create attributes and usage policy
  // bound to the given |pcr_map| of pcr_index -> pcr_value. The usage is
  // restricted by |key_type|. On success returns true and populates |key_blob|
  // with the TPM private key blob and |public_key_der| with the DER-encoded
  // public key. |creation_blob| is an opaque blob that must be passed back as
  // an input into VerifyPCRBoundKey.
  virtual bool CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 AsymmetricKeyUsage key_type,
                                 brillo::SecureBlob* key_blob,
                                 brillo::SecureBlob* public_key_der,
                                 brillo::SecureBlob* creation_blob) = 0;

  // Returns true if the given |key_blob| represents a SRK-wrapped key which
  // has both create attributes and usage policy bound to |pcr_map| of
  // pcr_index -> pcr_value. |creation_blob| is the blob containing creation
  // data, that was generated by CreatePCRBoundKey.
  virtual bool VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 const brillo::SecureBlob& key_blob,
                                 const brillo::SecureBlob& creation_blob) = 0;

  // Extends the PCR given by |pcr_index| with |extension|. The |extension| must
  // be exactly 20 bytes in length.
  virtual bool ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) = 0;

  // Reads the current |pcr_value| of the PCR given by |pcr_index|.
  virtual bool ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) = 0;

  // Checks to see if the endorsement key is available by attempting to get its
  // public key
  virtual bool IsEndorsementKeyAvailable() = 0;

  // Attempts to create the endorsement key in the TPM
  virtual bool CreateEndorsementKey() = 0;

  // Attempts to take ownership of the TPM
  //
  // Parameters
  //   max_timeout_tries - The maximum number of attempts to make if the call
  //                       times out, which it may occasionally do
  virtual bool TakeOwnership(int max_timeout_tries,
                             const brillo::SecureBlob& owner_password) = 0;

  // Wrapps a provided RSA key with the TPM's Storage Root Key.
  //
  // Parameters
  //   public_modulus - the public modulus of the provided Rsa key
  //   prime_factor - one of the prime factors of the Rsa key to wrap
  //   wrapped_key (OUT) - A blob representing the wrapped key
  virtual bool WrapRsaKey(const brillo::SecureBlob& public_modulus,
                          const brillo::SecureBlob& prime_factor,
                          brillo::SecureBlob* wrapped_key) = 0;

  // Loads an SRK-wrapped key into the TPM.
  //
  // Parameters
  //   wrapped_key - The blob (as produced by WrapRsaKey).
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  virtual hwsec::error::TPMErrorBase LoadWrappedKey(
      const brillo::SecureBlob& wrapped_key, ScopedKeyHandle* key_handle) = 0;

  // Loads the Cryptohome Key using a pre-defined UUID. This method does
  // nothing when using TPM2.0
  //
  // Parameters
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  //   key_blob (OUT) - If non-null, the blob representing this loaded key.
  virtual bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                       brillo::SecureBlob* key_blob) = 0;

  // Closes the TPM state associated with the given |key_handle|.
  virtual void CloseHandle(TpmKeyHandle key_handle) = 0;

  // Gets the TPM status information. If there |context| and |key| are supplied,
  // they will be used in encryption/decryption test. They can be 0 to bypass
  // the test.
  //
  // Parameters
  //   key - The optional key to check for encryption/decryption
  //   status (OUT) - The TpmStatusInfo structure containing the results
  virtual void GetStatus(base::Optional<TpmKeyHandle> key,
                         TpmStatusInfo* status) = 0;

  // Returns whether the TPM SRK is vulnerable to the ROCA vulnerability.
  // An empty optional is returned when the result is unknown due to an error.
  // This method always returns |false| when using TPM 2.0.
  virtual hwsec::error::TPMErrorBase IsSrkRocaVulnerable(bool* result) = 0;

  // Gets the current state of the dictionary attack logic. Returns false on
  // failure.
  virtual bool GetDictionaryAttackInfo(int* counter,
                                       int* threshold,
                                       bool* lockout,
                                       int* seconds_remaining) = 0;

  // Resets DA lock. This call requires owner permissions. For TPM 1.2,
  // |delegate_blob| and |delegate_secret| for an owner delegate must be
  // provided. For TPM 2.0, everything is handled in tpm_managerd and those 2
  // args are unused.
  virtual bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) = 0;

  // For TPMs with updateable firmware: Declate the current firmware
  // version stable and invalidate previous versions, if any.
  // For TPMs with fixed firmware: NOP.
  virtual void DeclareTpmFirmwareStable() = 0;

  // Performs TPM-specific actions to remove the specified |dependency| on
  // retaining the TPM owner password. When all dependencies have been removed
  // the owner password can be cleared.
  // Returns true if the dependency has been successfully removed or was
  // already removed by the time this function is called.
  virtual bool RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) = 0;

  // Clears the stored owner password.
  // Returns true if the password is cleared by this method, or was already
  // clear when we called it.
  virtual bool ClearStoredPassword() = 0;

  // Obtains version information from the TPM.
  virtual bool GetVersionInfo(TpmVersionInfo* version_info) = 0;

  // Obtains field upgrade status for IFX TPMs.
  virtual bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) = 0;

  // Obtains RSU device id from TPM for the Remote Server Unlock process.
  // Due to the privacy reasons, this id must not be used for any purpose other
  // than verifying RMA unlock eligibility.
  virtual bool GetRsuDeviceId(std::string* device_id) = 0;

  // Get a pointer to the LECredentialBackend object, which is used to call the
  // relevant TPM commands necessary to implement Low Entropy (LE) credential
  // protection.
  //
  // If the Tpm implementation does not support LE credential handling,
  // this function will return a nullptr.
  virtual LECredentialBackend* GetLECredentialBackend() = 0;

  // Get a pointer to the SignatureSealingBackend object, which is used for
  // performing signature-sealing operations. Returns nullptr if the
  // implementation does not support signature-sealing operations.
  virtual SignatureSealingBackend* GetSignatureSealingBackend() = 0;

  // Gets owner auth delegate. Returns |true| iff the operation succeeds. Once
  // returning |true|, |blob| and |secret| are set to the blob and secret of
  // owner auth delegate, respectively. |has_reset_lock_permissions| indicates
  // whether the delegate has permissions to call |TPM_ResetLockValue|.
  //
  // For TPM2.0, the implementaion could be an exception, which is returning
  // |true| without the output data. The definition of this interface would
  // therefore be a little bit awkward because this interface should be private
  // to TPM1.2. Yet, due to the current design of |ServiceDistributed| we need
  // to expose this interface for now. Need to remove the all the delegate
  // interace at once after proper refactoring.
  virtual bool GetDelegate(brillo::Blob* blob,
                           brillo::Blob* secret,
                           bool* has_reset_lock_permissions) = 0;

  // Returns whether the owner auth delegate set by is bound to PCR.
  virtual hwsec::error::TPMErrorBase IsDelegateBoundToPcr(bool* result) = 0;

  // Returns whether the owner auth delegate set by has reset lock permissions.
  virtual bool DelegateCanResetDACounter() = 0;

  // Returns the map with expected PCR values for the user.
  // TODO(crbug.com/1065907): The username and use_extended_pcr params do not
  // belong in the tpm class layer, and long term should be refactored out. See
  // bug.
  virtual std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username, bool use_extended_pcr) const = 0;

  // Derive the |auth_value| from |pass_blob| using |key_handle|.
  // The input |pass_blob| must have 256 bytes. For TPM2.0, |key_handle| is used
  // to decrypt the |pass_blob|, obtaining the authorization value. For TPM1.2
  // the |key_handle| is unused.
  virtual hwsec::error::TPMErrorBase GetAuthValue(
      base::Optional<TpmKeyHandle> key_handle,
      const brillo::SecureBlob& pass_blob,
      brillo::SecureBlob* auth_value) = 0;

 private:
  static Tpm* singleton_;
  static base::Lock singleton_lock_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_H_
