// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/user_secret_stash.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"

namespace cryptohome {
namespace {

constexpr char kEnableUssFeatureTestFlagName[] = "uss_enabled";
constexpr char kDisableUssFeatureTestFlagName[] = "uss_disabled";

std::optional<bool>& GetUserSecretStashExperimentOverride() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to checking whether flag file exists.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

bool EnableUssFeatureTestFlagFileExists(Platform* platform) {
  return DoesFlagFileExist(kEnableUssFeatureTestFlagName, platform);
}

bool DisableUssFeatureTestFlagFileExists(Platform* platform) {
  return DoesFlagFileExist(kDisableUssFeatureTestFlagName, platform);
}

// Returns the UserSecretStash experiment flag value.
UssExperimentFlag UserSecretStashExperimentResult(Platform* platform) {
  // 1. If the state is overridden by unit tests, return this value.
  if (GetUserSecretStashExperimentOverride().has_value()) {
    return GetUserSecretStashExperimentOverride().value()
               ? UssExperimentFlag::kEnabled
               : UssExperimentFlag::kDisabled;
  }
  // 2. If no unittest override defer to checking the feature test file
  // existence. The disable file precedes the enable file.
  if (DisableUssFeatureTestFlagFileExists(platform)) {
    return UssExperimentFlag::kDisabled;
  }
  if (EnableUssFeatureTestFlagFileExists(platform)) {
    return UssExperimentFlag::kEnabled;
  }
  // 3. Without overrides, the behavior is to always enable UserSecretStash
  // experiment.
  return UssExperimentFlag::kEnabled;
}

}  // namespace

bool IsUserSecretStashExperimentEnabled(Platform* platform) {
  return UserSecretStashExperimentResult(platform) ==
         UssExperimentFlag::kEnabled;
}

void ResetUserSecretStashExperimentForTesting() {
  GetUserSecretStashExperimentOverride().reset();
}

std::optional<bool> SetUserSecretStashExperimentForTesting(
    std::optional<bool> enabled) {
  std::optional<bool> original = GetUserSecretStashExperimentOverride();
  GetUserSecretStashExperimentOverride() = enabled;
  return original;
}

}  // namespace cryptohome
