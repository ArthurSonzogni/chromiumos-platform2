// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/umtest_utils.h"

#include "update_engine/update_manager/policy_utils.h"

namespace chromeos_update_manager {

void PrintTo(const EvalStatus& status, ::std::ostream* os) {
  *os << ToString(status);
}

}  // namespace chromeos_update_manager
