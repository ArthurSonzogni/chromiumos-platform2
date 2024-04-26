// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <memory>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libstorage/platform/platform.h>
#include <vboot/tlcl.h>

#include "init/tpm_encryption/tpm.h"

namespace encryption {
namespace {

// TPM2 NVRAM area and related constants.
const uint32_t kNvramAreaTpm2Magic = 0x54504D32;
const uint32_t kNvramAreaTpm2VersionMask = 0x000000FF;
const uint32_t kNvramAreaTpm2CurrentVersion = 1;

constexpr uint32_t kAttributesMask =
    TPMA_NV_PPWRITE | TPMA_NV_OWNERWRITE | TPMA_NV_AUTHWRITE |
    TPMA_NV_POLICYWRITE | TPMA_NV_POLICY_DELETE | TPMA_NV_WRITEALL |
    TPMA_NV_WRITEDEFINE | TPMA_NV_WRITE_STCLEAR | TPMA_NV_GLOBALLOCK |
    TPMA_NV_PPREAD | TPMA_NV_OWNERREAD | TPMA_NV_AUTHREAD | TPMA_NV_POLICYREAD |
    TPMA_NV_NO_DA | TPMA_NV_ORDERLY | TPMA_NV_CLEAR_STCLEAR |
    TPMA_NV_PLATFORMCREATE | TPMA_NV_READ_STCLEAR;
const uint32_t kAttributes = TPMA_NV_AUTHWRITE | TPMA_NV_AUTHREAD |
                             TPMA_NV_WRITEDEFINE | TPMA_NV_READ_STCLEAR;

struct nvram_area_tpm2 {
  uint32_t magic;
  uint32_t ver_flags;
  uint8_t key_material[SHA256_DIGEST_LENGTH];
};

bool IsSpaceDefinedCorrectly(NvramSpace* space) {
  uint32_t attributes = 0;
  bool rc = space->GetAttributes(&attributes);
  if (!rc) {
    LOG(ERROR) << "Failed to get NVRAM space attributes: " << rc;
    return false;
  }

  if ((attributes & kAttributesMask) != kAttributes) {
    LOG(ERROR) << "Bad NVRAM space attributes: " << attributes;
    return false;
  }

  return true;
}

// Derive the system key from the key material in |area|.
brillo::SecureBlob DeriveSystemKey(const struct nvram_area_tpm2* area) {
  return hwsec_foundation::Sha256(brillo::SecureBlob(
      area->key_material, area->key_material + sizeof(area->key_material)));
}

}  // namespace

const uint8_t* kOwnerSecret = nullptr;
const size_t kOwnerSecretSize = 0;

class Tpm2SystemKeyLoader : public SystemKeyLoader {
 public:
  explicit Tpm2SystemKeyLoader(Tpm* tpm) : tpm_(tpm) {}
  Tpm2SystemKeyLoader(const Tpm2SystemKeyLoader&) = delete;
  Tpm2SystemKeyLoader& operator=(const Tpm2SystemKeyLoader&) = delete;

  bool Load(brillo::SecureBlob* key) override;
  bool Initialize(const brillo::SecureBlob& key_material,
                  brillo::SecureBlob* derived_system_key) override;
  bool Persist() override;
  void Lock() override;
  bool SetupTpm() override;
  bool GenerateForPreservation(brillo::SecureBlob* previous_key,
                               brillo::SecureBlob* fresh_key) override;
  bool CheckLockbox(bool* valid) override;
  bool UsingLockboxKey() override;

 private:
  Tpm* tpm_ = nullptr;

  // Provisional space contents that get initialized by Generate() and written
  // to the NVRAM space by Persist();
  std::unique_ptr<brillo::SecureBlob> provisional_contents_;
};

// For TPM2, NVRAM area is separate from Lockbox.
// Cases:
//  - wrong-size NVRAM or invalid write-locked NVRAM: tampered with / corrupted
//    ignore
//    will never have the salt in NVRAM (finalization_needed forever)
//    return FAIL_FATAL (will re-create the mounts, if existed)
//  - read-locked NVRAM: already started / tampered with
//    ignore
//    return FAIL_FATAL (will re-create the mounts, if existed)
//  - no NVRAM or invalid but not write-locked NVRAM: OOBE or interrupted OOBE
//    generate new salt, write to NVRAM, write-lock, read-lock
//    return SUCCESS
//  - valid NVRAM not write-locked: interrupted OOBE
//    use NVRAM, write-lock, read-lock
//    (security-wise not worse than finalization_needed forever)
//    return SUCCESS
//  - valid NVRAM:
//    use NVRAM, read-lock
//    return SUCCESS
//
// In case of success: (NVRAM area found and used)
//  - *system_key populated with NVRAM area entropy.
// In case of failure: (NVRAM missing or error)
//  - *system_key untouched.
bool Tpm2SystemKeyLoader::Load(brillo::SecureBlob* system_key) {
  LOG(INFO) << "Getting key from TPM2 NVRAM index " << kEncStatefulIndex;

  if (!tpm_->available()) {
    return false;
  }

  NvramSpace* encstateful_space = tpm_->GetEncStatefulSpace();
  if (!encstateful_space->is_valid()) {
    LOG(INFO) << "NVRAM area doesn't exist or is invalid";
    return false;
  }

  bool rc = IsSpaceDefinedCorrectly(encstateful_space);
  if (!rc) {
    return false;
  }

  if (encstateful_space->contents().size() < sizeof(struct nvram_area_tpm2)) {
    LOG(ERROR) << "Too small NVRAM space: "
               << encstateful_space->contents().size();
    return false;
  }
  const struct nvram_area_tpm2* area =
      reinterpret_cast<const struct nvram_area_tpm2*>(
          encstateful_space->contents().data());
  if (area->magic != kNvramAreaTpm2Magic ||
      (area->ver_flags & kNvramAreaTpm2VersionMask) !=
          kNvramAreaTpm2CurrentVersion) {
    return false;
  }

  *system_key = DeriveSystemKey(area);
  return true;
}

bool Tpm2SystemKeyLoader::Initialize(const brillo::SecureBlob& key_material,
                                     brillo::SecureBlob* derived_system_key) {
  provisional_contents_ =
      std::make_unique<brillo::SecureBlob>(sizeof(nvram_area_tpm2));
  struct nvram_area_tpm2* area =
      reinterpret_cast<struct nvram_area_tpm2*>(provisional_contents_->data());
  area->magic = kNvramAreaTpm2Magic;
  area->ver_flags = kNvramAreaTpm2CurrentVersion;

  size_t key_material_size = key_material.size();
  if (key_material_size != sizeof(area->key_material)) {
    LOG(ERROR) << "Invalid key material size " << key_material_size;
    return false;
  }
  memcpy(area->key_material, key_material.data(), key_material_size);

  if (derived_system_key) {
    *derived_system_key = DeriveSystemKey(area);
  }

  return true;
}

bool Tpm2SystemKeyLoader::Persist() {
  CHECK(provisional_contents_);

  NvramSpace* encstateful_space = tpm_->GetEncStatefulSpace();
  if (IsSpaceDefinedCorrectly(encstateful_space) != true) {
    bool rc = encstateful_space->Define(kAttributes,
                                        sizeof(struct nvram_area_tpm2), 0);
    if (!rc) {
      LOG(ERROR) << "Failed to define NVRAM space.";
      return false;
    }
  }

  bool rc = encstateful_space->Write(*provisional_contents_);
  if (!rc) {
    uint32_t attributes = 0;
    encstateful_space->GetAttributes(&attributes);
    LOG(ERROR) << "Failed to write NVRAM area. Attributes: " << attributes;
    return false;
  }

  return true;
}

void Tpm2SystemKeyLoader::Lock() {
  // Lock the area as needed. Write-lock may be already set.
  // Read-lock is never set at this point, since we were able to read.
  // Not being able to lock is not fatal, though exposes the key.
  uint32_t attributes = 0;
  NvramSpace* encstateful_space = tpm_->GetEncStatefulSpace();
  if (encstateful_space->GetAttributes(&attributes) != true) {
    LOG(ERROR) << "Failed to read attributes";
    return;
  }

  if (!(attributes & TPMA_NV_WRITELOCKED) &&
      encstateful_space->WriteLock() != true) {
    LOG(ERROR) << "Failed to write-lock NVRAM area.";
  }
  if (encstateful_space->ReadLock() != true) {
    LOG(ERROR) << "Failed to read-lock NVRAM area.";
  }
}

bool Tpm2SystemKeyLoader::SetupTpm() {
  // NVRAM indexes can be defined without requiring special privileges, so
  // there's nothing to do here.
  return true;
}

bool Tpm2SystemKeyLoader::GenerateForPreservation(
    brillo::SecureBlob* previous_key, brillo::SecureBlob* fresh_key) {
  LOG(FATAL) << "Preservation not implemented for TPM 2.0";
}

bool Tpm2SystemKeyLoader::CheckLockbox(bool* valid) {
  // Lockbox is valid only once the TPM is owned.
  return tpm_->IsOwned(valid);
}

bool Tpm2SystemKeyLoader::UsingLockboxKey() {
  // TPM 2 systems never fall back to using the lockbox salt.
  return false;
}

std::unique_ptr<SystemKeyLoader> SystemKeyLoader::Create(
    libstorage::Platform* platform,
    Tpm* tpm,
    const base::FilePath& rootdir,
    const base::FilePath& stateful_mount) {
  return std::make_unique<Tpm2SystemKeyLoader>(tpm);
}

}  // namespace encryption
