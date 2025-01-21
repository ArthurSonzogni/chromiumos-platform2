// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/siginfo_description.h"

#include <string.h>

#include <base/strings/stringprintf.h>

namespace login_manager {

std::string GetExitDescription(const siginfo_t& status) {
  return status.si_code == CLD_EXITED
             ? base::StringPrintf("exit code %d", status.si_status)
             : base::StringPrintf("signal %d (%s)", status.si_status,
                                  strsignal(status.si_status));
}

}  // namespace login_manager
