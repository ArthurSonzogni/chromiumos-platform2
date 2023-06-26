// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_PAIRING_KEY_STORAGE_IMPL_H_
#define BIOD_PAIRING_KEY_STORAGE_IMPL_H_

#include "biod/pairing_key_storage.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

namespace biod {

// This class handles the persistent storage of the pairing secret (Pk). It is
// only established once per powerwash cycle, and on every boot we need to load
// the Pk back to the AuthStack.
class PairingKeyStorageImpl : public PairingKeyStorage {
 public:
  explicit PairingKeyStorageImpl(const std::string& root_path,
                                 const std::string& auth_stack_name);
  PairingKeyStorageImpl(const PairingKeyStorageImpl&) = delete;
  PairingKeyStorageImpl& operator=(const PairingKeyStorageImpl&) = delete;
  ~PairingKeyStorageImpl() override = default;

  // PairingKeyStorage overrides:
  bool PairingKeyExists() override;
  std::optional<brillo::Blob> ReadWrappedPairingKey() override;
  bool WriteWrappedPairingKey(const brillo::Blob& wrapped_pairing_key) override;

 private:
  base::FilePath pk_dir_path_;
  base::FilePath pk_file_path_;
};

}  // namespace biod

#endif  // BIOD_PAIRING_KEY_STORAGE_IMPL_H_
