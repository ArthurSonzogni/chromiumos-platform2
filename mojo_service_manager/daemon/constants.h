// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_
#define MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_

namespace chromeos {
namespace mojo_service_manager {

// The path of the service manager's socket server.
constexpr char kSocketPath[] = "/run/mojo/service_manager";

// The policy directory path.
constexpr char kPolicyDirectoryPath[] = "/etc/mojo/service_manager/policy";

// The extra policy directory path which is only used in dev mode.
constexpr char kExtraPolicyDirectoryPathInDevMode[] =
    "/usr/local/etc/mojo/service_manager/policy";

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_
