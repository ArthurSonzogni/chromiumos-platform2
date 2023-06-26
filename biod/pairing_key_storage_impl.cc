// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/pairing_key_storage_impl.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <brillo/files/file_util.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>

namespace biod {

namespace {
constexpr char kWrappedPairingKeyFilename[] = "wrapped_pk";
}  // namespace

PairingKeyStorageImpl::PairingKeyStorageImpl(const std::string& root_path,
                                             const std::string& auth_stack_name)
    : pk_dir_path_(base::FilePath(root_path).Append(auth_stack_name)),
      pk_file_path_(pk_dir_path_.Append(kWrappedPairingKeyFilename)) {}

bool PairingKeyStorageImpl::PairingKeyExists() {
  return base::PathExists(pk_file_path_);
}

std::optional<brillo::Blob> PairingKeyStorageImpl::ReadWrappedPairingKey() {
  if (!base::PathExists(pk_file_path_)) {
    LOG(ERROR) << "ReadWrappedPairingKey when Pk doesn't exist.";
    return std::nullopt;
  }

  std::string wrapped_pairing_key;
  if (!base::ReadFileToString(pk_file_path_, &wrapped_pairing_key)) {
    LOG(ERROR) << "Failed to read wrapped_pk file.";
    return std::nullopt;
  }

  return brillo::BlobFromString(wrapped_pairing_key);
}

bool PairingKeyStorageImpl::WriteWrappedPairingKey(
    const brillo::Blob& wrapped_pairing_key) {
  {
    brillo::ScopedUmask owner_only_umask(~(0700));

    if (!base::CreateDirectory(pk_dir_path_)) {
      PLOG(ERROR) << "Cannot create directory " << pk_dir_path_ << ".";
      return false;
    }
  }

  {
    brillo::ScopedUmask owner_only_umask(~(0600));

    if (!base::ImportantFileWriter::WriteFileAtomically(
            pk_file_path_, brillo::BlobToString(wrapped_pairing_key))) {
      LOG(ERROR) << "Failed to write wrapped Pk file.";
      return false;
    }
  }
  return true;
}

}  // namespace biod
