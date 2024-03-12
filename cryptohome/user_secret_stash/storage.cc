// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/storage.h"

#include <sys/stat.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

// Use rw------- for the USS files.
constexpr mode_t kUserSecretStashFilePermissions = S_IRUSR | S_IWUSR;

}  // namespace

UssStorage::UssStorage(libstorage::Platform* platform) : platform_(platform) {}

CryptohomeStatus UssStorage::Persist(
    const brillo::Blob& uss_container_flatbuffer,
    const ObfuscatedUsername& obfuscated_username) {
  const base::FilePath path =
      UserSecretStashPath(obfuscated_username, kUserSecretStashDefaultSlot);

  ReportTimerStart(kUSSPersistTimer);
  bool file_write_failure = !platform_->WriteFileAtomicDurable(
      path, uss_container_flatbuffer, kUserSecretStashFilePermissions);
  ReportTimerStop(kUSSPersistTimer);

  if (file_write_failure) {
    LOG(ERROR) << "Failed to store the UserSecretStash file for "
               << obfuscated_username;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSStorageWriteFailedInPersist),
        ErrorActionSet({PossibleAction::kReboot,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<brillo::Blob> UssStorage::LoadPersisted(
    const ObfuscatedUsername& obfuscated_username) const {
  const base::FilePath path =
      UserSecretStashPath(obfuscated_username, kUserSecretStashDefaultSlot);
  brillo::Blob uss_container_flatbuffer;

  ReportTimerStart(kUSSLoadPersistedTimer);
  bool file_read_failure =
      !platform_->ReadFile(path, &uss_container_flatbuffer);
  ReportTimerStop(kUSSLoadPersistedTimer);

  if (file_read_failure) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUSSStorageReadFailedInLoadPersisted),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kDeleteVault,
                        PossibleAction::kAuth,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return uss_container_flatbuffer;
}

}  // namespace cryptohome
