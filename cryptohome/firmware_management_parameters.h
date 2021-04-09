// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// FirmwareManagementParameters - class for storing firmware management
// parameters to TPM
#ifndef CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_H_
#define CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_H_

#include <memory>

#include <base/macros.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>
#include <openssl/sha.h>

#include "cryptohome/tpm.h"

namespace cryptohome {

struct FirmwareManagementParametersRawV1_0;

// FirmwareManagementParameters (FWMP, for short) stores firmware management
// parameters to the TPM.
//
// This class provides system integration using TPM NVRAM permissions ensure
// that firmware settings cannot be modified without the TPM owner password or
// a persistent root-level compromise of the device.
//
// FirmwareManagementParameters is not thread-safe and should not be accessed
// in parallel.
//
// A normal usage flow for FirmwareManagementParameters would be something as
// follows:
//
// Initializing new data in the FWMP (except with error checking :)
//   FirmwareManagementParameters fwmp(tpm);
//   fwmp->Create();
//   fwmp->Store(dev_flags, dev_hash);
//
// Reading back the data can be done along these lines:
//   FirmwareManagementParameters fwmp(tpm, kNvramSpace);
//   fwmp->GetFlags(&dev_flags);
//   fwmp->GetDeveloperKeyHash(&dev_hash);
// ...
class FirmwareManagementParameters {
 public:
  // TODO(b/183474803): add doc.
  enum class ResetMethod {
    kRecreateSpace,
    kStoreDefaultFlags,
  };
  enum class WriteProtectionMethod {
    kWriteLock,
    kOwnerAuthorization,
  };
  // Creates a propoer firmware management parameters according to the TPM
  // version on the device.
  static std::unique_ptr<FirmwareManagementParameters> CreateInstance(Tpm* tpm);

  // Populates the basic internal state of the firmware management parameters.
  //
  // Parameters
  // - reset_method: how we reset the content of FWMP. See `ResetMethod`.
  // - write_protection_method: how we prevent the content from being modified
  // after write. See `WriteProtectionMethod`.
  // - tpm: a required pointer to a TPM object.
  //
  // FirmwareManagementParameters requires a |tpm|.  If a NULL |tpm| is
  // supplied, none of the operations will succeed, but it should not crash or
  // behave unexpectedly. See firmware_management_parameters.md for info.
  FirmwareManagementParameters(ResetMethod reset_method,
                               WriteProtectionMethod write_protection_method,
                               Tpm* tpm);
  FirmwareManagementParameters(const FirmwareManagementParameters&) = delete;
  FirmwareManagementParameters& operator=(const FirmwareManagementParameters&) =
      delete;

  virtual ~FirmwareManagementParameters();

  // Creates the backend state needed for this firmware management parameters.
  //
  // Instantiates a new TPM NVRAM index to store the FWMP data.
  //
  // Returns
  // - true if a new space was instantiated or an old one could be used.
  // - false if the space cannot be created or claimed.
  virtual bool Create(void);

  // Destroys all backend state for this firmware management parameters.
  //
  // This call deletes the NVRAM space if defined.
  //
  // Returns
  // - false if TPM Owner authorization is missing or the space cannot be
  //   destroyed.
  // - true if the space is already undefined or has been destroyed.
  virtual bool Destroy(void);

  // Loads the TPM NVRAM state date into memory
  //
  // Returns
  // - true if TPM NVRAM data is properly retrieved.
  // - false if the NVRAM data does not exist or is invalid.
  virtual bool Load(void);

  // Commits the in-memory data to TPM NVRAM
  //
  // Parameters
  // - flags: New value of flags
  // - developer_key_hash: New dev key hash value; may be NULL to skip setting
  //   hash
  // Returns
  // - true if data was properly stored.
  // - false if the NVRAM data does not exist or is invalid.
  virtual bool Store(uint32_t flags, const brillo::Blob* developer_key_hash);

  // Returns the saved flags
  // Parameters
  // - flags: Current value of flags
  // Returns
  // - true if flags are properly retrieved.
  // - false if the NVRAM data does not exist or is invalid.
  virtual bool GetFlags(uint32_t* flags);

  // Returns the saved developer key hash
  // Parameters
  // - hash: Current value of developer key hash
  // Returns
  // - true if hash is properly retrieved.
  // - false if the NVRAM data does not exist or is invalid.
  virtual bool GetDeveloperKeyHash(brillo::Blob* hash);

  // Returns true if the firmware management parameters have been loaded
  virtual bool IsLoaded() const { return loaded_; }

  // NVRAM index for firmware management parameters space
  static const uint32_t kNvramIndex;
  // Size of the NVRAM structure
  static const uint32_t kNvramBytes;
  // Offset of CRC'd data (past CRC and size)
  static const uint32_t kCrcDataOffset;

 private:
  // Returns true if we have the authorization needed to create/destroy
  // NVRAM spaces.
  bool HasAuthorization() const;

  // Returns true if the tpm is owned and connected.
  bool TpmIsReady() const;

  const ResetMethod reset_method_;
  const WriteProtectionMethod write_protection_method_;
  Tpm* const tpm_;
  std::unique_ptr<FirmwareManagementParametersRawV1_0> raw_;
  bool loaded_ = false;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_H_
