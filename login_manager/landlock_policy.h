// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_LANDLOCK_POLICY_H_
#define LOGIN_MANAGER_LANDLOCK_POLICY_H_

#include <libminijail.h>

#include <string_view>

#include <base/containers/span.h>

namespace login_manager {

// A class that provides a Landlock policy for use with Minijail.
class LandlockPolicy {
 public:
  LandlockPolicy();
  LandlockPolicy(const LandlockPolicy&) = delete;
  LandlockPolicy& operator=(const LandlockPolicy&) = delete;

  ~LandlockPolicy();

  // Gets a snapshot of the current policy.
  // Only exposed for testing.
  base::span<const std::string_view> GetPolicySnapshotForTesting();

  // Adds a policy to the supplied Minijail.
  void SetupPolicy(minijail* j);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_LANDLOCK_POLICY_H_
