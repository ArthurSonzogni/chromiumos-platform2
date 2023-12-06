// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_ID_FLEX_STATE_KEY_H_
#define FLEX_ID_FLEX_STATE_KEY_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>

namespace flex_id {
// This class is responsible for generating and saving
// a sufficiently random machine identifier.
class FlexStateKeyGenerator {
 public:
  explicit FlexStateKeyGenerator(const base::FilePath& base_path);

  // Reads the contents of
  // mnt/stateful_partition/unencrypted/preserve/flex/flex_state_key which is
  // where the flex_state_key is preserved when performing a powerwash.
  std::optional<std::string> TryPreservedFlexStateKey();

  // Reads the contents of var/lib/flex_id/flex_state_key which is
  // the flex_state_key.
  std::optional<std::string> ReadFlexStateKey();

  // Generates a new value for a flex_state_key.
  std::optional<std::string> GenerateFlexStateKey();

  // Writes the flex_state_key to var/lib/flex_id/flex_state_key.
  bool WriteFlexStateKey(const std::string& flex_state_key);

  // Tries to find and return a state key in the following order:
  // 1. Existing state key
  // 2. Powerwash preserved state key
  // 3. Newly generated state key
  // The result is saved to var/lib/flex_id/flex_state_key
  std::optional<std::string> GenerateAndSaveFlexStateKey();

 private:
  base::FilePath base_path_;
};

}  // namespace flex_id

#endif  // FLEX_ID_FLEX_STATE_KEY_H_
