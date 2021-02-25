// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains implementation for TpmPersistentState class.

#include "cryptohome/tpm_persistent_state.h"

#include <base/files/file_path.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

const FilePath kTpmStatusFile("/mnt/stateful_partition/.tpm_status");
const FilePath kOpenCryptokiPath("/var/lib/opencryptoki");

TpmPersistentState::TpmPersistentState(Platform* platform)
    : platform_(platform) {}

bool TpmPersistentState::SetSealedPassword(const SecureBlob& sealed_password) {
  base::AutoLock lock(tpm_status_lock_);

  if (!LoadTpmStatus()) {
    return false;
  }

  tpm_status_.set_flags(TpmStatus::OWNED_BY_THIS_INSTALL |
                        TpmStatus::USES_RANDOM_OWNER |
                        TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                        TpmStatus::ATTESTATION_NEEDS_OWNER);
  tpm_status_.set_owner_password(sealed_password.data(),
                                 sealed_password.size());

  if (!StoreTpmStatus()) {
    tpm_status_.clear_owner_password();
    return false;
  }
  return true;
}

bool TpmPersistentState::SetDefaultPassword() {
  base::AutoLock lock(tpm_status_lock_);

  if (!LoadTpmStatus()) {
    return false;
  }

  tpm_status_.set_flags(TpmStatus::OWNED_BY_THIS_INSTALL |
                        TpmStatus::USES_WELL_KNOWN_OWNER |
                        TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                        TpmStatus::ATTESTATION_NEEDS_OWNER);
  tpm_status_.clear_owner_password();

  return StoreTpmStatus();
}

bool TpmPersistentState::GetSealedPassword(SecureBlob* sealed_password) {
  base::AutoLock lock(tpm_status_lock_);

  if (!LoadTpmStatus()) {
    return false;
  }

  if (!(tpm_status_.flags() & TpmStatus::OWNED_BY_THIS_INSTALL)) {
    return false;
  }
  if ((tpm_status_.flags() & TpmStatus::USES_WELL_KNOWN_OWNER)) {
    sealed_password->clear();
    return true;
  }
  if (!(tpm_status_.flags() & TpmStatus::USES_RANDOM_OWNER) ||
      !tpm_status_.has_owner_password()) {
    return false;
  }

  sealed_password->resize(tpm_status_.owner_password().size());
  tpm_status_.owner_password().copy(sealed_password->char_data(),
                                    tpm_status_.owner_password().size(), 0);
  return true;
}

bool TpmPersistentState::ClearDependency(TpmOwnerDependency dependency) {
  base::AutoLock lock(tpm_status_lock_);

  int32_t flag_to_clear;
  switch (dependency) {
    case TpmOwnerDependency::kInstallAttributes:
      flag_to_clear = TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER;
      break;
    case TpmOwnerDependency::kAttestation:
      flag_to_clear = TpmStatus::ATTESTATION_NEEDS_OWNER;
      break;
    default:
      return false;
  }

  if (!LoadTpmStatus()) {
    return false;
  }
  if (!(tpm_status_.flags() & flag_to_clear)) {
    return true;
  }
  tpm_status_.set_flags(tpm_status_.flags() & ~flag_to_clear);
  return StoreTpmStatus();
}

bool TpmPersistentState::ClearStoredPasswordIfNotNeeded() {
  base::AutoLock lock(tpm_status_lock_);

  if (!LoadTpmStatus()) {
    return false;
  }

  int32_t dependency_flags = TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER |
                             TpmStatus::ATTESTATION_NEEDS_OWNER;
  if (tpm_status_.flags() & dependency_flags) {
    // The password is still needed, do not clear.
    return false;
  }

  if (!tpm_status_.has_owner_password()) {
    return true;
  }
  tpm_status_.clear_owner_password();
  return StoreTpmStatus();
}

bool TpmPersistentState::ClearStatus() {
  base::AutoLock lock(tpm_status_lock_);

  // Ignore errors: just a cleanup - kOpenCryptokiPath is not used.
  platform_->DeleteFileDurable(kOpenCryptokiPath);
  // Ignore errors: we will overwrite the status later.
  platform_->DeleteFileDurable(kTpmStatusFile);
  tpm_status_.Clear();
  tpm_status_.set_flags(TpmStatus::NONE);
  read_tpm_status_ = true;
  return true;
}

bool TpmPersistentState::LoadTpmStatus() {
  if (read_tpm_status_) {
    return true;
  }
  if (!platform_->FileExists(kTpmStatusFile)) {
    tpm_status_.Clear();
    tpm_status_.set_flags(TpmStatus::NONE);
    read_tpm_status_ = true;
    return true;
  }
  brillo::Blob file_data;
  if (!platform_->ReadFile(kTpmStatusFile, &file_data)) {
    return false;
  }
  tpm_status_.Clear();
  if (!tpm_status_.ParseFromArray(file_data.data(), file_data.size())) {
    return false;
  }
  read_tpm_status_ = true;
  return true;
}

bool TpmPersistentState::StoreTpmStatus() {
  if (platform_->FileExists(kTpmStatusFile)) {
    // Shred old status file, not very useful on SSD. :(
    int64_t file_size;
    if (platform_->GetFileSize(kTpmStatusFile, &file_size)) {
      const auto random = CryptoLib::CreateSecureRandomBlob(file_size);
      platform_->WriteSecureBlobToFile(kTpmStatusFile, random);
      platform_->DataSyncFile(kTpmStatusFile);
    }
    platform_->DeleteFile(kTpmStatusFile);
  }

  SecureBlob final_blob(tpm_status_.ByteSizeLong());
  tpm_status_.SerializeWithCachedSizesToArray(
      static_cast<google::protobuf::uint8*>(final_blob.data()));
  return platform_->WriteSecureBlobToFileAtomicDurable(kTpmStatusFile,
                                                       final_blob, 0600);
}

}  // namespace cryptohome
