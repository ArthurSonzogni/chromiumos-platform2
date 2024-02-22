// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_INSTALLER_H_
#define DLCSERVICE_INSTALLER_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <base/gtest_prod_util.h>
#include <brillo/errors/error.h>
#include <dbus/update_engine/dbus-constants.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/types.h"

namespace dlcservice {

class InstallerInterface {
 public:
  using InstallSuccessCallback = base::OnceCallback<void()>;
  using InstallFailureCallback = base::OnceCallback<void(brillo::Error*)>;

  using OnReadyCallback = base::OnceCallback<void(bool)>;

  struct InstallArgs {
    DlcId id;
    std::string url;
    bool scaled = false;
    bool force_ota = false;
  };

  struct Status {
    enum class State {
      OK = 0,
      CHECKING = 1,
      DOWNLOADING = 2,
      VERIFYING = 3,
      NOT_FOUND = 100,
      ERROR = 200,
      BLOCKED = 999,
    };
    State state = State::OK;
    bool is_install = false;
    double progress = 0.;

    // Only update_engine specific field during installer transition.
    // NOTE: Use if you know when it's valid.
    update_engine::ErrorCode last_attempt_error;
  };

  // Observers of installer status changes/syncs.
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;
    virtual void OnStatusSync(const Status& status) = 0;
  };

  InstallerInterface() = default;
  virtual ~InstallerInterface() = default;

  // Adds an observer instance to the observers list to listen.
  virtual void AddObserver(Observer* observer) = 0;

  // Removes an observer from observers list.
  virtual void RemoveObserver(Observer* observer) = 0;

  // Initialization for tasks requiring IO/scheduling/etc.
  virtual bool Init() = 0;

  // Invoke to install based on `InstallArgs`.
  virtual void Install(const InstallArgs& install_args,
                       InstallSuccessCallback success_callback,
                       InstallFailureCallback failure_callback) = 0;

  // Indicates if the installer has reached a state ready for installation.
  virtual bool IsReady() = 0;

  // Callback to indicate if the installer has reached a state ready for
  // installation.
  virtual void OnReady(OnReadyCallback callback) = 0;

  // Sync any status that installer maintains.
  virtual void StatusSync() = 0;
};

class Installer : public InstallerInterface {
 public:
  Installer() = default;
  ~Installer() override = default;

  Installer(const Installer&) = delete;
  Installer& operator=(const Installer&) = delete;

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool Init() override;
  void Install(const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback) override;
  bool IsReady() override;
  void OnReady(OnReadyCallback callback) override;
  void StatusSync() override;

 protected:
  // Helper for `OnReady(..)` method.
  void ScheduleOnReady(OnReadyCallback callback, bool ready);

  // Helper for `StatusSync(..)` method.
  void NotifyStatusSync(const Status& status);

  // A list of observers that are listening.
  base::ObserverList<Observer> observers_;
};

class UpdateEngineInstaller : public Installer {
 public:
  UpdateEngineInstaller();
  ~UpdateEngineInstaller() override = default;

  UpdateEngineInstaller(const UpdateEngineInstaller&) = delete;
  UpdateEngineInstaller& operator=(const UpdateEngineInstaller&) = delete;

  bool Init() override;
  void Install(const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback) override;
  bool IsReady() override;
  void OnReady(OnReadyCallback callback) override;
  void StatusSync() override;

 private:
  FRIEND_TEST_ALL_PREFIXES(UpdateEngineInstallerWithBoolParamsTest,
                           OnReadyTest);
  FRIEND_TEST_ALL_PREFIXES(UpdateEngineInstallerWithStatusParamsTest,
                           StatusSyncTest);

  // Callback for `WaitForServiceToBeAvailable` from DBus.
  void OnWaitForUpdateEngineServiceToBeAvailable(bool available);

  // Registering into update_engine's signals.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);

  bool update_engine_service_available_ = false;

  std::vector<OnReadyCallback> on_ready_callbacks_;

  base::WeakPtrFactory<UpdateEngineInstaller> weak_ptr_factory_;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_INSTALLER_H_
