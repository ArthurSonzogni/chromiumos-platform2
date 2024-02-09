// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_ROLLBACK_PREFS_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_ROLLBACK_PREFS_H_

namespace chromeos_update_manager {

// Value used to represent that kernel key versions can always roll-forward.
// This is the maximum value of a kernel key version.
constexpr int kRollforwardInfinity = 0xfffffffe;

// Whether the device should roll back to the target version, and if yes, which
// type of rollback should it do. Matches chrome_device_policy.proto's
// AutoUpdateSettingsProto::RollbackToTargetVersion.
enum class RollbackToTargetVersion {
  kUnspecified = 0,
  kDisabled = 1,
  kRollbackAndPowerwash = 2,
  kRollbackAndRestoreIfPossible = 3,
  // This value must be the last entry.
  kMaxValue = 4
};

// Whether the device should do rollback and powerwash on channel downgrade.
// Matches chrome_device_policy.proto's
// |AutoUpdateSettingsProto::ChannelDowngradeBehavior|.
enum class ChannelDowngradeBehavior {
  kUnspecified = 0,
  kWaitForVersionToCatchUp = 1,
  kRollback = 2,
  kAllowUserToConfigure = 3,
  // These values must be kept up to date.
  kFirstValue = kUnspecified,
  kLastValue = kAllowUserToConfigure
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_ROLLBACK_PREFS_H_
