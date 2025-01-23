// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_H_
#define LOGIN_MANAGER_ARC_MANAGER_H_

#include <stdint.h>

#include <memory>
#include <string>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>

#include "login_manager/arc_sideload_status_interface.h"

namespace brillo::dbus_utils {
template <typename... Ts>
class DBusMethodResponse;
}  // namespace brillo::dbus_utils

namespace login_manager {

class ArcSideloadStatusInterface;
class InitDaemonController;
class SystemUtils;

// Manages ARC operations.
class ArcManager {
 public:
  ArcManager(SystemUtils& system_utils,
             std::unique_ptr<InitDaemonController> init_controller,
             std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);
  ArcManager(const ArcManager&) = delete;
  ArcManager& operator=(const ArcManager&) = delete;
  ~ArcManager();

  // Upstart signal triggered on ARC is booted.
  static constexpr char kArcBootedImpulse[] = "arc-booted";

  void Initialize();
  void Finalize();

  // TODO(crbug.com/390297821): expose some internal states or adding hooks
  // to be called for transition period. Remove them.
  bool IsAdbSideloadAllowed() const;
  void OnUpgradeArcContainer();

  // D-Bus method implementation.
  bool SetArcCpuRestriction(brillo::ErrorPtr* error,
                            uint32_t in_restriction_state);
  bool EmitArcBooted(brillo::ErrorPtr* error, const std::string& in_account_id);
  bool GetArcStartTimeTicks(brillo::ErrorPtr* error, int64_t* out_start_time);
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response);
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response);

 private:
  void EnableAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status,
      const char* error);
  void QueryAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status);

  const raw_ref<SystemUtils> system_utils_;
  std::unique_ptr<InitDaemonController> init_controller_;
  std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status_;

  // Timestamp when ARC container is upgraded.
  base::TimeTicks arc_start_time_;

  base::WeakPtrFactory<ArcManager> weak_factory_{this};
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_H_
