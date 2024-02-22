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
#include "dlcservice/utils.h"

namespace dlcservice {

void Installer::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void Installer::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

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

void Installer::StatusSync() {
  NotifyStatusSync({});
}

void Installer::NotifyStatusSync(const Status& status) {
  for (Observer& observer : observers_)
    observer.OnStatusSync(status);
}

UpdateEngineInstaller::UpdateEngineInstaller() : weak_ptr_factory_(this) {}

bool UpdateEngineInstaller::Init() {
  SystemState::Get()->set_installer_status(Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = false,
      .progress = 0.,
  });

  auto* update_engine = SystemState::Get()->update_engine();
  update_engine->GetObjectProxy()->WaitForServiceToBeAvailable(base::BindOnce(
      &UpdateEngineInstaller::OnWaitForUpdateEngineServiceToBeAvailable,
      base::Unretained(this)));
  update_engine->RegisterStatusUpdateAdvancedSignalHandler(
      base::BindRepeating(&UpdateEngineInstaller::OnStatusUpdateAdvancedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(
          &UpdateEngineInstaller::OnStatusUpdateAdvancedSignalConnected,
          weak_ptr_factory_.GetWeakPtr()));
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

void UpdateEngineInstaller::StatusSync() {
  SystemState::Get()->update_engine()->GetStatusAdvancedAsync(
      base::BindOnce(&UpdateEngineInstaller::OnStatusUpdateAdvancedSignal,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce([](brillo::Error* err) {
        if (err) {
          auto err_ptr = err->Clone();
          LOG(ERROR) << "Failed to get update_engine status, err="
                     << Error::ToString(err_ptr);
        }
      }));
}

void UpdateEngineInstaller::OnWaitForUpdateEngineServiceToBeAvailable(
    bool available) {
  update_engine_service_available_ = available;
  for (auto&& callback : on_ready_callbacks_) {
    Installer::ScheduleOnReady(std::move(callback), available);
  }
  on_ready_callbacks_.clear();
}

void UpdateEngineInstaller::OnStatusUpdateAdvancedSignal(
    const update_engine::StatusResult& status_result) {
  Status status;
  switch (status_result.current_operation()) {
    case update_engine::Operation::UPDATED_NEED_REBOOT:
      status.state = Status::State::BLOCKED;
      break;
    case update_engine::Operation::IDLE:
      status.state = Status::State::OK;
      break;
    case update_engine::Operation::REPORTING_ERROR_EVENT:
      status.state = Status::State::ERROR;
      break;
    case update_engine::Operation::VERIFYING:
      status.state = Status::State::VERIFYING;
      break;
    case update_engine::Operation::DOWNLOADING:
      status.state = Status::State::DOWNLOADING;
      break;
    default:
      status.state = Status::State::CHECKING;
      break;
  }
  status.is_install = status_result.is_install();
  status.progress = status_result.progress();

  Installer::NotifyStatusSync(status);
}

void UpdateEngineInstaller::OnStatusUpdateAdvancedSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) {
  if (!success) {
    LOG(ERROR) << AlertLogTag(kCategoryInit)
               << "Failed to connect to update_engine's StatusUpdate signal.";
  }
}

}  // namespace dlcservice
