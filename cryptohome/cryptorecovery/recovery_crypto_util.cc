// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

bool operator==(const OnboardingMetadata& lhs, const OnboardingMetadata& rhs) {
  return (lhs.cryptohome_user_type == rhs.cryptohome_user_type &&
          lhs.cryptohome_user == rhs.cryptohome_user &&
          lhs.device_user_id == rhs.device_user_id &&
          lhs.board_name == rhs.board_name &&
          lhs.form_factor == rhs.form_factor && lhs.rlz_code == rhs.rlz_code &&
          lhs.recovery_id == rhs.recovery_id);
}

bool operator!=(const OnboardingMetadata& lhs, const OnboardingMetadata& rhs) {
  return !(lhs == rhs);
}

}  // namespace cryptorecovery
}  // namespace cryptohome
