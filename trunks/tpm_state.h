// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_STATE_H_
#define TRUNKS_TPM_STATE_H_

#include <base/macros.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// TpmState is an interface which provides access to TPM state information.
class TRUNKS_EXPORT TpmState {
 public:
  TpmState() {}
  virtual ~TpmState() {}

  // Initializes based on the current TPM state. This method must be called once
  // before any other method. It may be called multiple times to refresh the
  // state information.
  virtual TPM_RC Initialize() = 0;

  // Returns true iff TPMA_PERMANENT:ownerAuthSet is set.
  virtual bool IsOwnerPasswordSet() = 0;

  // Returns true iff TPMA_PERMANENT:endorsementAuthSet is set.
  virtual bool IsEndorsementPasswordSet() = 0;

  // Returns true iff TPMA_PERMANENT:lockoutAuthSet is set.
  virtual bool IsLockoutPasswordSet() = 0;

  // Returns true iff owner, endorsement and lockout passwords are set.
  virtual bool IsOwned() = 0;

  // Returns true iff TPMA_PERMANENT:inLockout is set.
  virtual bool IsInLockout() = 0;

  // Returns true iff TPMA_STARTUP_CLEAR:phEnable is set.
  virtual bool IsPlatformHierarchyEnabled() = 0;

  // Returns true iff TPMA_STARTUP_CLEAR:shEnable is set.
  virtual bool IsStorageHierarchyEnabled() = 0;

  // Returns true iff TPMA_STARTUP_CLEAR:ehEnable is set.
  virtual bool IsEndorsementHierarchyEnabled() = 0;

  // Returns true iff shEnable and ehEnable are set and phEnable is clear.
  virtual bool IsEnabled() = 0;

  // Returns true iff TPMA_STARTUP_CLEAR:orderly is set.
  virtual bool WasShutdownOrderly() = 0;

  // Returns true iff the TPM supports RSA-2048 keys.
  virtual bool IsRSASupported() = 0;

  // Returns true iff the TPM supports the ECC NIST P-256 curve.
  virtual bool IsECCSupported() = 0;

  // Returns the current value of the Lockout counter.
  virtual uint32_t GetLockoutCounter() = 0;

  // Returns the maximum lockout failures allowed before the TPM goes into
  // lockout.
  virtual uint32_t GetLockoutThreshold() = 0;

  // Returns the number of seconds before the lockout counter will decrement.
  virtual uint32_t GetLockoutInterval() = 0;

  // Returns the number of seconds after a LockoutAuth failure before
  // LockoutAuth can be used again.
  virtual uint32_t GetLockoutRecovery() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TpmState);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_STATE_H_
