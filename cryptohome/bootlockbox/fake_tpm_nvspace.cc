// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "cryptohome/bootlockbox/fake_tpm_nvspace.h"

namespace cryptohome {

bool FakeTpmNVSpace::Initialize() {
  return true;
}

NVSpaceState FakeTpmNVSpace::DefineNVSpace() {
  return NVSpaceState::kNVSpaceUninitialized;
}

bool FakeTpmNVSpace::WriteNVSpace(const std::string& digest) {
  digest_ = digest;
  return true;
}

bool FakeTpmNVSpace::ReadNVSpace(std::string* digest, NVSpaceState* state) {
  *digest = digest_;
  *state = NVSpaceState::kNVSpaceNormal;
  return true;
}

bool FakeTpmNVSpace::LockNVSpace() {
  return true;
}

void FakeTpmNVSpace::RegisterOwnershipTakenCallback(
    const base::RepeatingClosure& callback) {
  std::move(callback).Run();
}

void FakeTpmNVSpace::SetDigest(const std::string& digest) {
  digest_ = digest;
}

}  // namespace cryptohome
