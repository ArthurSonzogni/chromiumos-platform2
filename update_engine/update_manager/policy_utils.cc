// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/policy_utils.h"

#include <string>

using std::string;

namespace chromeos_update_manager {

string ToString(EvalStatus status) {
  switch (status) {
    case EvalStatus::kFailed:
      return "kFailed";
    case EvalStatus::kSucceeded:
      return "kSucceeded";
    case EvalStatus::kAskMeAgainLater:
      return "kAskMeAgainLater";
    case EvalStatus::kContinue:
      return "kContinue";
  }
  return "Invalid";
}

}  // namespace chromeos_update_manager
