// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_system_utilities.h"

#include <cstdio>

#include <base/check.h>
#include <base/logging.h>

namespace diagnostics {

FakeSystemUtilities::FakeSystemUtilities() = default;
FakeSystemUtilities::~FakeSystemUtilities() = default;

int FakeSystemUtilities::Uname(struct utsname* buf) {
  DCHECK(buf);

  if (uname_ret_code_ == 0)
    snprintf(buf->machine, sizeof(utsname::machine), "%s",
             uname_machine_.c_str());

  return uname_ret_code_;
}

void FakeSystemUtilities::SetUnameResponse(
    int ret_code, const base::Optional<std::string>& machine) {
  uname_ret_code_ = ret_code;
  if (machine.has_value())
    uname_machine_ = machine.value();
}

}  // namespace diagnostics
