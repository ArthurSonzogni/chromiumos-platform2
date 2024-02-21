// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_INSTALLER_H_
#define DLCSERVICE_INSTALLER_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <brillo/errors/error.h>

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

  InstallerInterface() = default;
  virtual ~InstallerInterface() = default;

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
};

class Installer : public InstallerInterface {
 public:
  Installer() = default;
  ~Installer() override = default;

  Installer(const Installer&) = delete;
  Installer& operator=(const Installer&) = delete;

  bool Init() override;
  void Install(const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback) override;
  bool IsReady() override;
  void OnReady(OnReadyCallback callback) override;

 protected:
  // Helper for `OnReady(..)` method.
  void ScheduleOnReady(OnReadyCallback callback, bool ready);
};

class UpdateEngineInstaller : public Installer {
 public:
  UpdateEngineInstaller() = default;
  ~UpdateEngineInstaller() override = default;

  UpdateEngineInstaller(const UpdateEngineInstaller&) = delete;
  UpdateEngineInstaller& operator=(const UpdateEngineInstaller&) = delete;

  bool Init() override;
  void Install(const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback) override;
  bool IsReady() override;
  void OnReady(OnReadyCallback callback) override;

 private:
  // Callback for `WaitForServiceToBeAvailable` from DBus.
  void OnWaitForUpdateEngineServiceToBeAvailable(bool available);

  bool update_engine_service_available_ = false;

  std::vector<OnReadyCallback> on_ready_callbacks_;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_INSTALLER_H_
