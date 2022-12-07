// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "bootlockbox/fake_tpm_nvspace.h"

namespace bootlockbox {

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

NVSpaceState FakeTpmNVSpace::ReadNVSpace(std::string* digest) {
  *digest = digest_;
  return NVSpaceState::kNVSpaceNormal;
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

}  // namespace bootlockbox
