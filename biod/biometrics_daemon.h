// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOMETRICS_DAEMON_H_
#define BIOD_BIOMETRICS_DAEMON_H_

#include <memory>
#include <vector>

#include <brillo/dbus/exported_object_manager.h>
#include <dbus/bus.h>

#include "biod/biometrics_manager_wrapper.h"
#include "biod/session_state_manager.h"

namespace biod {

class BiometricsDaemon {
 public:
  BiometricsDaemon();
  BiometricsDaemon(const BiometricsDaemon&) = delete;
  BiometricsDaemon& operator=(const BiometricsDaemon&) = delete;

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  std::unique_ptr<brillo::dbus_utils::ExportedObjectManager> object_manager_;
  // BiometricsManagerWrapper holds raw pointers to SessionStateManager and
  // ExportedObjectManager so it must be placed after them to make sure that
  // pointers remain valid (destruction order is correct).
  std::vector<std::unique_ptr<BiometricsManagerWrapper>> biometrics_managers_;
};
}  // namespace biod

#endif  // BIOD_BIOMETRICS_DAEMON_H_
