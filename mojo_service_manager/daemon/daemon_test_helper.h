// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_DAEMON_TEST_HELPER_H_
#define MOJO_SERVICE_MANAGER_DAEMON_DAEMON_TEST_HELPER_H_

#include <sysexits.h>

namespace chromeos {
namespace mojo_service_manager {

// The exit code for daemon test helper.
enum class DaemonTestHelperResult {
  kResetWithOsError = EX__MAX + 1,
  kConnectSuccessfully,
};

// The fake socket name for testing.
inline constexpr char kTestSocketName[] = "test_socket";

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_DAEMON_TEST_HELPER_H_
