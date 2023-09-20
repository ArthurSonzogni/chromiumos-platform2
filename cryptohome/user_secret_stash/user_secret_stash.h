// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_
#define CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_

#include <optional>

#include <brillo/secure_blob.h>

#include "cryptohome/platform.h"

namespace cryptohome {

// Structure defining the override state for key block with existing wrapping
// ID.
enum class OverwriteExistingKeyBlock {
  kEnabled,
  kDisabled,
};

// Returns whether the UserSecretStash experiment (using the USS instead of
// vault keysets) is enabled.
// The experiment is controlled by fetching a config file from gstatic. It
// matches the local USS version returned by
// `UserSecretStashExperimentVersion()` and the `last_invalid` version specified
// in the config file. If our version is greater, the experiment is enabled with
// `population` probability, and disabled otherwise. Whether the experiment is
// enabled can be overridden by creating the /var/lib/cryptohome/uss_enabled (to
// enable) or the /var/lib/cryptohome/uss_disabled (to disable) file. Unit tests
// can furthermore override this behavior using
// `SetUserSecretStashExperimentForTesting()`.
bool IsUserSecretStashExperimentEnabled(Platform* platform);

// Allows to toggle the experiment state in tests. Passing nullopt reverts to
// the default behavior. Returns the original contents before setting to allow
// tests to restore the original value.
std::optional<bool> SetUserSecretStashExperimentForTesting(
    std::optional<bool> enabled);

// This resets the static |uss_experiment_enabled| flag to simulate
// restarting cryptohomed process in the unittests.
void ResetUserSecretStashExperimentForTesting();

// RAII-style object that allows you to set the USS experiment flag (enabling or
// disabling it) in tests. The setting you apply will be cleared on destruction.
// You can use it both within individual tests by creating it on the stack, or
// in an entire fixture as a member variable.
class [[nodiscard]] SetUssExperimentOverride {
 public:
  explicit SetUssExperimentOverride(bool enabled) {
    original_value_ = SetUserSecretStashExperimentForTesting(enabled);
  }
  SetUssExperimentOverride(const SetUssExperimentOverride&) = delete;
  SetUssExperimentOverride& operator=(const SetUssExperimentOverride&) = delete;
  ~SetUssExperimentOverride() {
    SetUserSecretStashExperimentForTesting(original_value_);
  }

 private:
  std::optional<bool> original_value_;
};
// Helper that construct a SetUssExperimentOverride with the appropriate
// boolean. Generally more readable than manually constructing one with a
// boolean flag. Normally invoked by using one of:
//
//   auto uss = EnableUssExperiment();
//   auto no_uss = DisableUssExperiment();
//
inline SetUssExperimentOverride EnableUssExperiment() {
  return SetUssExperimentOverride(true);
}
inline SetUssExperimentOverride DisableUssExperiment() {
  return SetUssExperimentOverride(false);
}

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_
