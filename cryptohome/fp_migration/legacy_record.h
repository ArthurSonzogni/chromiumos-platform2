// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FP_MIGRATION_LEGACY_RECORD_H_
#define CRYPTOHOME_FP_MIGRATION_LEGACY_RECORD_H_

#include <string>

namespace cryptohome {

// LegacyRecord keeps metadata in a legacy fingerprint template. It mirrors
// a protobuf definition in biod.
struct LegacyRecord final {
  std::string legacy_record_id;
  // Each legacy fingerprint has a user specified name. It was recorded in a
  // fingerprint template record, as the field |label| in biod::LegacyRecord.
  // This is not to be confused with the label of an auth factor.
  std::string user_specified_name;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FP_MIGRATION_LEGACY_RECORD_H_
