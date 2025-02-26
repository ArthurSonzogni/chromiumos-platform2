// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_MANAGER_H_
#define LOGIN_MANAGER_ARC_MANAGER_H_

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <dbus/login_manager/dbus-constants.h>

#include "login_manager/arc_sideload_status_interface.h"
#include "login_manager/dbus_adaptors/org.chromium.ArcManager.h"
#include "login_manager/login_metrics.h"

namespace arc {
class UpgradeArcContainerRequest;
}  // namespace arc

namespace brillo {

class ProcessReaper;

namespace dbus_utils {
template <typename... Ts>
class DBusMethodResponse;
}  // namespace dbus_utils
}  // namespace brillo

namespace dbus {
class Error;
class ObjectProxy;
}  // namespace dbus

namespace login_manager {

class ArcSideloadStatusInterface;
class ContainerManagerInterface;
class InitDaemonController;
class SystemUtils;

// Manages ARC operations.
class ArcManager : public org::chromium::ArcManagerInterface {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Sends D-Bus signal about ARC instance stop.
    // TODO(crbug.com/390297821): Move the signal to ARC D-Bus
    // service and remove this.
    virtual void SendArcInstanceStoppedSignal(uint32_t value) = 0;
  };

  // Creates an instance under surrounding context taken as arguments.
  // Referred arguments must be outlive the ArcManager instance.
  ArcManager(SystemUtils& system_utils,
             LoginMetrics& login_metrics,
             brillo::ProcessReaper& process_reaper,
             scoped_refptr<dbus::Bus> bus);
  ArcManager(const ArcManager&) = delete;
  ArcManager& operator=(const ArcManager&) = delete;
  ~ArcManager();

  static std::unique_ptr<ArcManager> CreateForTesting(
      SystemUtils& system_utils,
      LoginMetrics& login_metrics,
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<InitDaemonController> init_controller,
      dbus::ObjectProxy* debugd_proxy,
      std::unique_ptr<ContainerManagerInterface> android_container,
      std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);

  // Upstart signal triggered on ARC is booted.
  static constexpr char kStartArcInstanceImpulse[] = "start-arc-instance";
  static constexpr char kStopArcInstanceImpulse[] = "stop-arc-instance";
  static constexpr char kContinueArcBootImpulse[] = "continue-arc-boot";

  static constexpr char kArcBootedImpulse[] = "arc-booted";

  // ARC related impulse (systemd unit start or Upstart signal).
  static constexpr char kStopArcVmInstanceImpulse[] = "stop-arcvm-instance";

  // TODO(b/205032502): Because upgrading the container from mini to full often
  // takes more than 25 seconds, increasing it to accommodate P99.9.
  // Considering its cyclic nature setting it to 40 sec should cover majority
  // of P99.9 cases.
  static constexpr base::TimeDelta kArcBootContinueTimeout = base::Seconds(40);

  // TODO(b:66919195): Optimize Android container shutdown time. It
  // needs as long as 3s on kevin to perform graceful shutdown.
  static constexpr base::TimeDelta kContainerTimeout = base::Seconds(3);

  void Initialize();
  void Finalize();

  // Starts ArcManager D-Bus service.
  bool StartDBusService();

  void SetDelegate(std::unique_ptr<Delegate> delegate);

  // TODO(crbug.com/390297821): called from SessionManagerService.
  // Expose this as D-Bus method.
  void EmitStopArcVmInstanceImpulse();
  void RequestJobExit(int32_t reason);
  void EnsureJobExit(int64_t timeout_ms);

  // D-Bus method implementation.
  void OnUserSessionStarted(const std::string& in_account_id) override;
  bool StartArcMiniContainer(brillo::ErrorPtr* error,
                             const std::vector<uint8_t>& in_request) override;
  bool UpgradeArcContainer(brillo::ErrorPtr* error,
                           const std::vector<uint8_t>& in_request) override;
  bool StopArcInstance(brillo::ErrorPtr* error,
                       const std::string& account_id,
                       bool should_backup_log) override;
  bool SetArcCpuRestriction(brillo::ErrorPtr* error,
                            uint32_t in_restriction_state) override;
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id) override;
  bool GetArcStartTimeTicks(brillo::ErrorPtr* error,
                            int64_t* out_start_time) override;
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
             LoginMetrics& login_metrics,
             scoped_refptr<dbus::Bus> bus,
             std::unique_ptr<InitDaemonController> init_controller,
             dbus::ObjectProxy* debugd_proxy,
             std::unique_ptr<ContainerManagerInterface> android_container,
             std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status);

#if USE_CHEETS
  // Starts the Android container for ARC. If an error occurs, brillo::Error
  // instance is set to |error_out|.  After this succeeds, in case of ARC stop,
  // OnAndroidContainerStopped() is called.
  bool StartArcContainer(const std::vector<std::string>& env_vars,
                         brillo::ErrorPtr* error_out);

  // Creates environment variables passed to upstart for container upgrade.
  std::vector<std::string> CreateUpgradeArcEnvVars(
      const arc::UpgradeArcContainerRequest& request,
      const std::string& account_id,
      pid_t pid);

  // Called when the container fails to continue booting.
  void OnContinueArcBootFailed();

  // Stops the ARC container with the given |reason|.
  bool StopArcInstanceInternal(ArcContainerStopReason reason);

  // Called when the Android container is stopped.
  void OnAndroidContainerStopped(pid_t pid, ArcContainerStopReason reason);

  LoginMetrics::ArcContinueBootImpulseStatus GetArcContinueBootImpulseStatus(
      dbus::Error* dbus_error);
#endif

  void EnableAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status,
      const char* error);
  void QueryAdbSideloadCallbackAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      ArcSideloadStatusInterface::Status status);

  bool IsAdbSideloadAllowed() const;
  void OnUpgradeArcContainer();
  void BackupArcBugReport(const std::string& account_id);
  void DeleteArcBugReportBackup(const std::string& account_id);

  std::unique_ptr<Delegate> delegate_;

  const raw_ref<SystemUtils> system_utils_;
  const raw_ref<LoginMetrics> login_metrics_;

  // Interfaces to communicate with D-Bus system.
  scoped_refptr<dbus::Bus> bus_;
  const std::unique_ptr<InitDaemonController> init_controller_;
  dbus::ObjectProxy* const debugd_proxy_;
  org::chromium::ArcManagerAdaptor adaptor_{this};

  // ARC structures.
  const std::unique_ptr<ContainerManagerInterface> android_container_;
  std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status_;

  // Set of started user sessions represented by ID.
  std::set<std::string> user_sessions_;

  // Timestamp when ARC container is upgraded.
  base::TimeTicks arc_start_time_;

  std::unique_ptr<DBusService> dbus_service_;

  base::WeakPtrFactory<ArcManager> weak_factory_{this};
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_MANAGER_H_
