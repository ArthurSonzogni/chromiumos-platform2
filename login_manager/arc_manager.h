// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_H_
#define LOGIN_MANAGER_ARC_MANAGER_H_

#include <memory>
#include <string>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <dbus/login_manager/dbus-constants.h>

#include "login_manager/arc_sideload_status_interface.h"
#include "login_manager/dbus_adaptors/org.chromium.ArcManager.h"

namespace brillo::dbus_utils {
template <typename... Ts>
class DBusMethodResponse;
}  // namespace brillo::dbus_utils

namespace dbus {
class ObjectProxy;
}  // namespace dbus

namespace login_manager {

class ArcSideloadStatusInterface;
class InitDaemonController;
class SystemUtils;

// Manages ARC operations.
class ArcManager : public org::chromium::ArcManagerInterface {
 public:
  // Creates an instance under surrounding context taken as arguments.
  // Referred arguments must be outlive the ArcManager instance.
  ArcManager(SystemUtils& system_utils, scoped_refptr<dbus::Bus> bus);
  ArcManager(const ArcManager&) = delete;
  ArcManager& operator=(const ArcManager&) = delete;
  ~ArcManager();

  static std::unique_ptr<ArcManager> CreateForTesting(
      SystemUtils& system_utils,
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<InitDaemonController> init_controller,
      dbus::ObjectProxy* debugd_proxy,
      std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);

  static constexpr char kArcBootedImpulse[] = "arc-booted";

  // ARC related impulse (systemd unit start or Upstart signal).
  static constexpr char kStopArcVmInstanceImpulse[] = "stop-arcvm-instance";

  void Initialize();
  void Finalize();

  // Starts ArcManager D-Bus service.
  bool StartDBusService();

  // D-Bus method implementation.
  void OnUserSessionStarted(const std::string& in_account_id) override;
  void EmitStopArcVmInstanceImpulse() override;
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id) override;
  void EnableAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;
  void QueryAdbSideload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response)
      override;

 private:
  class DBusService;

  // Shared constructor with CreateForTesting.
  ArcManager(SystemUtils& system_utils,
             scoped_refptr<dbus::Bus> bus,
             std::unique_ptr<InitDaemonController> init_controller,
             dbus::ObjectProxy* debugd_proxy,
             std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);

  void EnableAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status,
      const char* error);
  void QueryAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status);

  void DeleteArcBugReportBackup(const std::string& account_id);

  const raw_ref<SystemUtils> system_utils_;

  // Interfaces to communicate with D-Bus system.
  scoped_refptr<dbus::Bus> bus_;
  const std::unique_ptr<InitDaemonController> init_controller_;
  dbus::ObjectProxy* const debugd_proxy_;
  org::chromium::ArcManagerAdaptor adaptor_{this};

  // ARC structures.
  std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status_;

  std::unique_ptr<DBusService> dbus_service_;

  base::WeakPtrFactory<ArcManager> weak_factory_{this};
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_H_
