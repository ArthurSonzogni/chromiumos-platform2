// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FWMP_CHECKER_PLATFORM_INDEX_H_
#define CRYPTOHOME_FWMP_CHECKER_PLATFORM_INDEX_H_

#include "cryptohome/fwmp_checker.h"

#include <tpm_manager/client/tpm_manager_utility.h>

namespace cryptohome {

class FwmpCheckerPlatformIndex : public FwmpChecker {
 public:
  FwmpCheckerPlatformIndex() = default;
  explicit FwmpCheckerPlatformIndex(
      tpm_manager::TpmManagerUtility* tpm_manager_utility);
  ~FwmpCheckerPlatformIndex() override = default;

  bool IsValidForWrite(uint32_t nv_index) override;

 private:
  // Initializes `tpm_manager_utility_`; returns `true` iff successful.
  bool InitializeTpmManagerUtility();

  tpm_manager::TpmManagerUtility* tpm_manager_utility_ = nullptr;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FWMP_CHECKER_PLATFORM_INDEX_H_
