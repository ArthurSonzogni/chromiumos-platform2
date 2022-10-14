// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTLOCKBOX_FAKE_TPM_NVSPACE_H_
#define BOOTLOCKBOX_FAKE_TPM_NVSPACE_H_

#include <string>

#include "bootlockbox/tpm_nvspace.h"

namespace bootlockbox {

class FakeTpmNVSpace : public TPMNVSpace {
 public:
  FakeTpmNVSpace() {}

  bool Initialize() override;

  NVSpaceState DefineNVSpace() override;

  bool WriteNVSpace(const std::string& digest) override;

  bool ReadNVSpace(std::string* digest, NVSpaceState* state) override;

  bool LockNVSpace() override;

  void RegisterOwnershipTakenCallback(
      const base::RepeatingClosure& callback) override;

  void SetDigest(const std::string& digest);

 private:
  std::string digest_;
};

}  // namespace bootlockbox

#endif  // BOOTLOCKBOX_FAKE_TPM_NVSPACE_H_
