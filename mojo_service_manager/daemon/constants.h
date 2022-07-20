// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_
#define MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_

namespace chromeos {
namespace mojo_service_manager {

// The path of the service manager's socket server.
inline constexpr char kSocketPath[] = "/run/mojo/service_manager.sock";

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_CONSTANTS_H_
