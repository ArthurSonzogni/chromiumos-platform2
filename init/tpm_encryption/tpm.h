// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Interface used by "mount-encrypted" to interface with the TPM.

#ifndef INIT_TPM_ENCRYPTION_TPM_H_
#define INIT_TPM_ENCRYPTION_TPM_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h>
#include <libstorage/platform/platform.h>
#include <openssl/sha.h>

namespace encryption {
const uint32_t kLockboxSizeV1 = 0x2c;
const uint32_t kLockboxSizeV2 = 0x45;

#if USE_TPM2
const uint32_t kLockboxIndex = 0x800004;
const uint32_t kEncStatefulIndex = 0x800005;
const uint32_t kEncStatefulSize = 40;
#else
const uint32_t kLockboxIndex = 0x20000004;
const uint32_t kEncStatefulIndex = 0x20000005;
const uint32_t kEncStatefulSize = 72;
#endif

const uint32_t kPCRBootMode = 0;

// Secret used for owner authorization. This is used for taking ownership and in
// TPM commands that require owner authorization. Currently, only the TPM 1.2
// implementation uses owner authorization for some of its operations. The
// constants are nullptr and zero, respectively, for TPM 2.0.
extern const BRILLO_EXPORT uint8_t* kOwnerSecret;
extern const BRILLO_EXPORT size_t kOwnerSecretSize;

// Path constants. Note that these don't carry the '/' root
// prefix because the actual path gets constructed relative to a valid stateful
// partition (which is a temporary directory in tests, the mounted
// unencrypted stateful for production).
namespace paths {
// Based on using root prefix:
inline constexpr char kFirmwareUpdateLocator[] =
    "usr/sbin/tpm-firmware-locate-update";
inline constexpr char kFirmwareDir[] = "lib/firmware/tpm";

// Based on /mnt/stateful prefix.
inline constexpr char kFirmwareUpdateRequest[] =
    "unencrypted/preserve/tpm_firmware_update_request";

namespace cryptohome {
inline constexpr char kTpmOwned[] = "unencrypted/tpm_manager/tpm_owned";
inline constexpr char kTpmStatus[] = ".tpm_status";
inline constexpr char kShallInitialize[] =
    "home/.shadow/.can_attempt_ownership";
inline constexpr char kAttestationDatabase[] =
    "unencrypted/preserve/attestation.epb";
}  // namespace cryptohome
}  // namespace paths

class Tpm;

class NvramSpace {
 public:
  NvramSpace(hwsec_foundation::TlclWrapper* tlcl, Tpm* tpm, uint32_t index);

  enum class Status {
    kUnknown,   // Not accessed yet.
    kAbsent,    // Not defined.
    kWritable,  // Defined but the content is not written (TPM1.2 only).
    kValid,     // Present and read was successful.
    kTpmError,  // Error accessing the space.
  };

  Status status() const { return status_; }
  bool is_valid() const { return status() == Status::kValid; }
  bool is_writable() const { return status() == Status::kWritable; }
  const brillo::SecureBlob& contents() const { return contents_; }

  // Resets the space so that it appears invalid. Doesn't update the TPM.
  void Reset();

  // Retrieves the space attributes.
  bool GetAttributes(uint32_t* attributes);

  // Attempts to read the NVRAM space.
  bool Read(uint32_t size);

  // Writes to the NVRAM space.
  bool Write(const brillo::SecureBlob& contents);

  // Sets the read lock on the space.
  bool ReadLock();

  // Sets write lock on the space.
  bool WriteLock();

  // Attempt to define the space with the given attributes and size.
  bool Define(uint32_t attributes, uint32_t size, uint32_t pcr_selection);

  // Check whether the space is bound to the specified PCR selection.
  bool CheckPCRBinding(uint32_t pcr_selection, bool* match);

 private:
  // Reads space definition parameters from the TPM.
  bool GetSpaceInfo();

  // Get the binding policy for the current PCR values of the given PCR
  // selection.
  bool GetPCRBindingPolicy(uint32_t pcr_selection,
                           std::vector<uint8_t>* policy);

  hwsec_foundation::TlclWrapper* tlcl_;
  Tpm* tpm_;
  uint32_t index_;

  // Cached copy of NVRAM space attributes.
  uint32_t attributes_;

  // Cached copy of the auth policy.
  std::vector<uint8_t> auth_policy_;

  // Cached copy of the data as read from the space.
  brillo::SecureBlob contents_;

  // Cached indicator reflecting the status of the space in the TPM.
  Status status_ = Status::kUnknown;
};

// Encapsulates high-level TPM state and the motions needed to open and close
// the TPM library.
class BRILLO_EXPORT Tpm {
 public:
  explicit Tpm(hwsec_foundation::TlclWrapper* tlcl);
  Tpm(const Tpm&) = delete;
  Tpm& operator=(const Tpm&) = delete;

  ~Tpm();

  bool available() const { return available_; }
  bool is_tpm2() const { return is_tpm2_; }

  bool IsOwned(bool* owned);

  bool GetRandomBytes(uint8_t* buffer, int wanted);

  // Returns the PCR value for PCR |index|, possibly from the cache.
  bool ReadPCR(uint32_t index, std::vector<uint8_t>* value);

  // Returns TPM version info.
  bool GetVersionInfo(uint32_t* vendor,
                      uint64_t* firmware_version,
                      std::vector<uint8_t>* vendor_specific);

  // Returns Infineon-specific field upgrade status.
  bool GetIFXFieldUpgradeInfo(TPM_IFX_FIELDUPGRADEINFO* field_upgrade_info);

  // Returns the initialized lockbox NVRAM space.
  NvramSpace* GetLockboxSpace();

  // Get the initialized encrypted stateful space.
  NvramSpace* GetEncStatefulSpace();

  // Take TPM ownership using an all-zeros password.
  bool TakeOwnership();

  // Set a flag in the TPM to indicate that the system key has been
  // re-initialized after the last TPM clear. The TPM automatically clears the
  // flag as a side effect of the TPM clear operation.
  bool SetSystemKeyInitializedFlag();

  // Check the system key initialized flag.
  bool HasSystemKeyInitializedFlag(bool* flag_value);

 private:
  bool available_ = false;
  bool is_tpm2_ = false;

  bool ownership_checked_ = false;
  bool owned_ = false;

#if !USE_TPM2
  bool initialized_flag_checked_ = false;
  bool initialized_flag_ = false;
#endif  // !USE_TPM2

  std::map<uint32_t, std::vector<uint8_t>> pcr_values_;

  std::unique_ptr<NvramSpace> lockbox_space_;
  std::unique_ptr<NvramSpace> encstateful_space_;

  hwsec_foundation::TlclWrapper* tlcl_;
};

// The interface used by the key handling logic to access the system key. The
// system key is used to wrap the actual data encryption key.
//
// System keys must have these properties:
//  1. The system key can only be accessed in the current boot mode, i.e.
//     switching to developer mode blocks access or destroys the system key.
//  2. A fresh system key must be generated after clearing the TPM. This can be
//     achieved either by arranging a TPM clear to drop the key or by detecting
//     a TPM clear an generating a fresh key.
//  3. The key should ideally not be accessible for reading after early boot.
//  4. Because mounting the encrypted stateful file system is on the critical
//     boot path, loading the system key must be reasonably fast.
//  5. Fresh keys can be generated with reasonable cost. Costly operations such
//     as taking TPM ownership after each TPM clear to set up fresh NVRAM spaces
//     do not fly performance-wise. The file system encryption key logic has a
//     fallback path to dump its key without protection by a system key until
//     the latter becomes available, but that's a risk that should ideally be
//     avoided.
class BRILLO_EXPORT SystemKeyLoader {
 public:
  virtual ~SystemKeyLoader() = default;

  // Create a system key loader suitable for the system.
  static std::unique_ptr<SystemKeyLoader> Create(
      libstorage::Platform* platform,
      Tpm* tpm,
      const base::FilePath& rootdir,
      const base::FilePath& stateful_mount);

  // Load the encryption key from TPM NVRAM. Returns true if successful and
  // fills in key, false if the key is not available or there is an error.
  virtual bool Load(brillo::SecureBlob* key) = 0;

  // Initializes system key NV space contents using |key_material|.
  // The size of |key_material| must equal SHA256_DIGEST_LENGTH. If
  // |derived_system_key| is not null, stores the derived system key into it.
  //
  // This function does not store the contents in NVRAM yet.
  //
  // Returns true if successful or false otherwise.
  virtual bool Initialize(const brillo::SecureBlob& key_material,
                          brillo::SecureBlob* derived_system_key) = 0;

  // Persist a previously generated system key in NVRAM. This may not be
  // possible in case the TPM is not in a state where the NVRAM spaces can be
  // manipulated.
  virtual bool Persist() = 0;

  // Lock the system key to prevent further manipulation.
  virtual void Lock() = 0;

  // Set up the TPM to allow generation of a system key. This is an expensive
  // operation that can take dozens of seconds depending on hardware so this
  // can't be used routinely.
  virtual bool SetupTpm() = 0;

  // Checks whether the system is eligible for encryption key preservation. If
  // so, sets up a new system key to wrap the existing encryption key. On
  // success, |previous_key| and |fresh_key| will be filled in. Returns false if
  // the system is not eligible or there is an error.
  virtual bool GenerateForPreservation(brillo::SecureBlob* previous_key,
                                       brillo::SecureBlob* fresh_key) = 0;

  // Checks whether the lockbox space contents are considered valid.
  virtual bool CheckLockbox(bool* valid) = 0;

  // Whether the lockbox salt is used as the system key.
  virtual bool UsingLockboxKey() = 0;
};

// A SystemKeyLoader implementation backed by a fixed system key supplied at
// construction time.
class FixedSystemKeyLoader : public SystemKeyLoader {
 public:
  explicit FixedSystemKeyLoader(const brillo::SecureBlob& key) : key_(key) {}

  bool Load(brillo::SecureBlob* key) override {
    *key = key_;
    return true;
  }
  bool Initialize(const brillo::SecureBlob& key_material,
                  brillo::SecureBlob* derived_system_key) override {
    return false;
  }
  bool Persist() override { return false; }
  void Lock() override {}
  bool SetupTpm() override { return false; }
  bool GenerateForPreservation(brillo::SecureBlob* previous_key,
                               brillo::SecureBlob* fresh_key) override {
    return false;
  }
  bool CheckLockbox(bool* valid) override { return false; }
  bool UsingLockboxKey() override { return false; }

 private:
  brillo::SecureBlob key_;
};

}  // namespace encryption
#endif  // INIT_TPM_ENCRYPTION_TPM_H_
