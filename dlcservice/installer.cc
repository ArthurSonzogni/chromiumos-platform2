// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/installer.h"

#include <utility>

#include <base/functional/bind.h>
#include <brillo/message_loops/message_loop.h>
#include <dbus/object_proxy.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/system_state.h"

namespace dlcservice {

bool Installer::Init() {
  return true;
}

void Installer::Install(const InstallArgs& install_args,
                        InstallSuccessCallback success_callback,
                        InstallFailureCallback failure_callback) {
  brillo::MessageLoop::current()->PostTask(FROM_HERE,
                                           std::move(success_callback));
}

bool Installer::IsReady() {
  return true;
}

void Installer::OnReady(OnReadyCallback callback) {
  ScheduleOnReady(std::move(callback), true);
}

void Installer::ScheduleOnReady(OnReadyCallback callback, bool ready) {
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::BindOnce([](OnReadyCallback callback,
                        bool ready) { std::move(callback).Run(ready); },
                     std::move(callback), ready));
}

bool UpdateEngineInstaller::Init() {
  // Default for update_engine status.
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::IDLE);
  status.set_is_install(false);
  SystemState::Get()->set_update_engine_status(status);
  SystemState::Get()
      ->update_engine()
      ->GetObjectProxy()
      ->WaitForServiceToBeAvailable(base::BindOnce(
          &UpdateEngineInstaller::OnWaitForUpdateEngineServiceToBeAvailable,
          base::Unretained(this)));
  return true;
}

void UpdateEngineInstaller::Install(const InstallArgs& install_args,
                                    InstallSuccessCallback success_callback,
                                    InstallFailureCallback failure_callback) {
  update_engine::InstallParams install_params;
  install_params.set_id(install_args.id);
  install_params.set_omaha_url(install_args.url);
  install_params.set_scaled(install_args.scaled);
  install_params.set_force_ota(install_args.force_ota);
  SystemState::Get()->update_engine()->InstallAsync(
      install_params, std::move(success_callback), std::move(failure_callback));
}

bool UpdateEngineInstaller::IsReady() {
  return update_engine_service_available_;
}

void UpdateEngineInstaller::OnReady(OnReadyCallback callback) {
  on_ready_callbacks_.push_back(std::move(callback));
  if (IsReady()) {
    OnWaitForUpdateEngineServiceToBeAvailable(true);
    return;
  }
  // Service is not available yet, waiting..
}

void UpdateEngineInstaller::OnWaitForUpdateEngineServiceToBeAvailable(
    bool available) {
  update_engine_service_available_ = available;
  for (auto&& callback : on_ready_callbacks_) {
    Installer::ScheduleOnReady(std::move(callback), available);
  }
  on_ready_callbacks_.clear();
}

}  // namespace dlcservice
