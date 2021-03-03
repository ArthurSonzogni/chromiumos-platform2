// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines interface for TpmPersistentState class.

#ifndef CRYPTOHOME_TPM_PERSISTENT_STATE_H_
#define CRYPTOHOME_TPM_PERSISTENT_STATE_H_

#include <brillo/secure_blob.h>

#include <base/synchronization/lock.h>

#include "cryptohome/tpm_status.pb.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

class Platform;

// Class for managing persistent tpm state stored in the filesystem.
// Lazily reads the current state into memory on the first access and
// caches it there for further accesses.
// Note: not thread-safe.
class TpmPersistentState {
 public:
  explicit TpmPersistentState(Platform* platform);

  // owner password is not stored, no dependencies are set.
  // Saves the updated state in the persistent storage before returning.
  // Returns true on success, false otherwise.
  bool ClearStatus();

  // Clears the specified dependency on the owner password in the state.
  // If there were any changes, saves the updated state in the persistent
  // storage before returning.
  // Returns true on success, false otherwise.
  bool ClearDependency(Tpm::TpmOwnerDependency dependency);

 private:
  // Loads TpmStatus that includes the owner password and the dependencies
  // from persistent storage, if not done yet. Caches TpmStatus in memory
  // after the first access. Subsequent Load's return success w/o re-reading
  // the data from persistent storage.
  bool LoadTpmStatus();

  // Saves the updated TpmStatus (in memory and in the persistent storage).
  // Returns true on success, false otherwise.
  bool StoreTpmStatus();

  Platform* platform_;

  // Protects access to data members held by this class.
  mutable base::Lock tpm_status_lock_;

  // Cached TpmStatus (read_tpm_status_ tells if was already read and cached).
  // TODO(apronin): replace with std::optional / base::Optional when available.
  bool read_tpm_status_ = false;
  TpmStatus tpm_status_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_PERSISTENT_STATE_H_
