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

bool TpmPersistentState::ClearDependency(Tpm::TpmOwnerDependency dependency) {
  base::AutoLock lock(tpm_status_lock_);

  int32_t flag_to_clear;
  switch (dependency) {
    case Tpm::TpmOwnerDependency::kInstallAttributes:
      flag_to_clear = TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER;
      break;
    case Tpm::TpmOwnerDependency::kAttestation:
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
