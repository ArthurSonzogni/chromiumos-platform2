// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/flatbuffer_file.h"

#include <sys/stat.h>

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

constexpr mode_t kOwnerReadWritePermissions = 0600;

}  // namespace

FlatbufferFile::FlatbufferFile(Platform* platform, const base::FilePath& path)
    : platform_(platform), path_(path) {}

FlatbufferFile::~FlatbufferFile() = default;

CryptohomeStatus FlatbufferFile::StoreFile(const brillo::Blob& buffer,
                                           const TimerType& timer_type) const {
  ReportTimerStart(timer_type);
  bool write_success = platform_->WriteFileAtomicDurable(
      path_, buffer, kOwnerReadWritePermissions);
  ReportTimerStop(timer_type);

  if (!write_success) {
    LOG(ERROR) << "Failed to store the file: " << path_.value();
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocStoreFileFailedInFlatbufferFile),
        ErrorActionSet({PossibleAction::kReboot,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<brillo::Blob> FlatbufferFile::LoadFile(
    const TimerType& timer_type) const {
  brillo::Blob buffer;
  ReportTimerStart(timer_type);
  bool read_success = platform_->ReadFile(path_, &buffer);
  ReportTimerStop(timer_type);

  if (!read_success) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocLoadFileFailedInFlatbufferFile),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kDeleteVault,
                        PossibleAction::kAuth,
                        PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return buffer;
}

}  // namespace cryptohome
