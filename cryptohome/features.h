// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FEATURES_H_
#define CRYPTOHOME_FEATURES_H_

#include "featured/feature_library.h"

namespace cryptohome {

// Control switch value for enabling backup VaultKeyset creation with USS.
inline constexpr struct VariationsFeature
    kCrOSLateBootMigrateToUserSecretStash = {
        .name = "CrOSLateBootMigrateToUserSecretStash",
        .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FEATURES_H_
