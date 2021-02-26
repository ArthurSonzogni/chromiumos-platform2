// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TpmInit - public interface class for initializing the TPM
#ifndef CRYPTOHOME_TPM_INIT_H_
#define CRYPTOHOME_TPM_INIT_H_

#include <memory>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>

#include "cryptohome/tpm.h"
#include "cryptohome/tpm_persistent_state.h"

namespace cryptohome {

class Platform;
class TpmInitTask;

class TpmInit {
  // Friend class TpmInitTask as it is a glue class to allow ThreadMain to be
  // called on a separate thread without inheriting from
  // PlatformThread::Delegate.
  friend class TpmInitTask;

 public:
  using OwnershipCallback =
      base::Callback<void(bool status, bool took_ownership)>;

  TpmInit(Tpm* tpm, Platform* platform);
  TpmInit(const TpmInit&) = delete;
  TpmInit& operator=(const TpmInit&) = delete;

  virtual ~TpmInit();

  // Sets the TPM to the state where we last left it in. This must be called
  // before the *TakeOwnership functions below, if we need to.
  //
  // Parameters:
  //   load_key - TRUE to load load Cryptohome key.
  //
  // Returns false if the instance has already been setup.
  virtual bool SetupTpm(bool load_key);

  // Returns true if the TPM is initialized and ready for use.
  virtual bool IsTpmReady();

  // Returns true if the TPM is enabled.
  virtual bool IsTpmEnabled();

  // Returns true if the TPM is owned.
  virtual bool IsTpmOwned();

  // Marks the TPM as being or not being been owned.
  virtual void SetTpmBeingOwned(bool being_owned);

  // Removes the given owner dependency. When all dependencies have been removed
  // the owner password can be cleared.
  //
  // Parameters
  //   dependency - The dependency (on TPM ownership) to be removed
  virtual void RemoveTpmOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency);

  virtual void set_tpm(Tpm* value);

  virtual Tpm* get_tpm();

  virtual bool HasCryptohomeKey();

  virtual TpmKeyHandle GetCryptohomeKey();

  virtual bool ReloadCryptohomeKey();

 private:
  FRIEND_TEST(TpmInitTest, ContinueInterruptedInitializeSrk);

  // Invoked by SetupTpm to restore TPM state from saved state in storage.
  void RestoreTpmStateFromStorage();

  // Returns whether or not the TPM is enabled by checking a flag in the TPM's
  // entry in either /sys/class/misc or /sys/class/tpm.
  bool IsEnabledCheckViaSysfs(const base::FilePath& enabled_file);

  // Returns whether or not the TPM is owned by checking a flag in the TPM's
  // entry in either /sys/class/misc or /sys/class/tpm.
  bool IsOwnedCheckViaSysfs(const base::FilePath& owned_file);

  bool SaveCryptohomeKey(const brillo::SecureBlob& wrapped_key);

  Tpm::TpmRetryAction LoadCryptohomeKey(ScopedKeyHandle* key_handle);

  bool CreateCryptohomeKey();

  bool LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle);

  // Returns true if the first byte of the file |file_name| is "1".
  bool CheckSysfsForOne(const base::FilePath& file_name) const;

  // The background task for initializing the TPM, implemented as a
  // PlatformThread::Delegate.
  std::unique_ptr<TpmInitTask> tpm_init_task_;
  base::PlatformThreadHandle init_thread_;

  OwnershipCallback ownership_callback_;

  bool take_ownership_called_ = false;
  bool took_ownership_ = false;
  int64_t initialization_time_ = 0;
  Platform* platform_ = nullptr;
  TpmPersistentState tpm_persistent_state_;
  ScopedKeyHandle cryptohome_key_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_INIT_H_
