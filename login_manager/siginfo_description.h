// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SIGINFO_DESCRIPTION_H_
#define LOGIN_MANAGER_SIGINFO_DESCRIPTION_H_

#include <signal.h>

#include <string>

namespace login_manager {

// Returns a string like "exit code 1" or "signal 11 (Segmentation fault)"
// based on the contents of |status|. This is a helper method for logging
// information from handlers.
std::string GetExitDescription(const siginfo_t& status);

}  // namespace login_manager

#endif  // LOGIN_MANAGER_SIGINFO_DESCRIPTION_H_
