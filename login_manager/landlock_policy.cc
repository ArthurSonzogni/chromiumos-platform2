// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/landlock_policy.h"

namespace login_manager {

namespace {

// Landlock allowlisted paths.
constexpr std::string_view kAllowedPaths[] = {"/dev",
                                              "/home/chronos",
                                              "/home/user",
                                              "/media",
                                              "/mnt",
                                              "/opt",
                                              "/proc",
                                              "/run",
                                              "/sys/fs/cgroup/",
                                              "/tmp",
                                              "/usr/local",
                                              "/var/cache",
                                              "/var/lib",
                                              "/var/lock",
                                              "/var/log",
                                              "/var/spool/support",
                                              "/var/tmp"};

constexpr char kRootPath[] = "/";

}  // anonymous namespace

LandlockPolicy::LandlockPolicy() = default;

LandlockPolicy::~LandlockPolicy() = default;

base::span<const std::string_view>
LandlockPolicy::GetPolicySnapshotForTesting() {
  return base::make_span(kAllowedPaths);
}

void LandlockPolicy::SetupPolicy(minijail* j) {
  minijail_add_fs_restriction_rx(j, kRootPath);

  // Add paths to the Minijail.
  for (const auto& path : kAllowedPaths) {
    minijail_add_fs_restriction_advanced_rw(j, path.data());
  }
}

}  // namespace login_manager
