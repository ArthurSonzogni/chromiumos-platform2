// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "bootlockbox/fake_hwsec_space.h"

namespace bootlockbox {

bool FakeTpmSpace::Initialize() {
  return true;
}

SpaceState FakeTpmSpace::DefineSpace() {
  return SpaceState::kSpaceUninitialized;
}

bool FakeTpmSpace::WriteSpace(const std::string& digest) {
  digest_ = digest;
  return true;
}

SpaceState FakeTpmSpace::ReadSpace(std::string* digest) {
  *digest = digest_;
  return SpaceState::kSpaceNormal;
}

bool FakeTpmSpace::LockSpace() {
  return true;
}

void FakeTpmSpace::RegisterOwnershipTakenCallback(
    const base::RepeatingClosure& callback) {
  std::move(callback).Run();
}

void FakeTpmSpace::SetDigest(const std::string& digest) {
  digest_ = digest;
}

}  // namespace bootlockbox
