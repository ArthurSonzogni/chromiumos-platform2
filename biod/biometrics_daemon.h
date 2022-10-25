// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOMETRICS_DAEMON_H_
#define BIOD_BIOMETRICS_DAEMON_H_

#include <memory>
#include <vector>

#include <brillo/dbus/exported_object_manager.h>
#include <dbus/bus.h>
#include <libhwsec/factory/factory_impl.h>

#include "biod/auth_stack_manager_wrapper.h"
#include "biod/biod_metrics.h"
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
  // Raw pointer to BiodMetrics is passed to CrosFpDevice (owned by
  // CrosFpBiometricsManager), CrosFpBiometricsManager (one of the
  // biometrics managers) and SessionStateManager. This line must be placed
  // above these object to ensure correct destruction order.
  std::unique_ptr<BiodMetricsInterface> biod_metrics_;
  std::unique_ptr<SessionStateManager> session_state_manager_;
  std::unique_ptr<brillo::dbus_utils::ExportedObjectManager> object_manager_;
  // BiometricsManagerWrappers and AuthStackManagers hold raw pointers to
  // SessionStateManager and ExportedObjectManager so it must be placed after
  // them to make sure that pointers remain valid (destruction order is
  // correct).
  std::vector<std::unique_ptr<BiometricsManagerWrapper>> biometrics_managers_;
  std::vector<std::unique_ptr<AuthStackManagerWrapper>> auth_stack_managers_;
  hwsec::FactoryImpl hwsec_factory_ =
      hwsec::FactoryImpl(hwsec::ThreadingMode::kCurrentThread);
};
}  // namespace biod

#endif  // BIOD_BIOMETRICS_DAEMON_H_
