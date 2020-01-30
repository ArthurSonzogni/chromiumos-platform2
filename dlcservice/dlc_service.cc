// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_service.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/utils.h"

using base::Callback;
using base::File;
using base::FilePath;
using base::ScopedTempDir;
using brillo::MessageLoop;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {

// Sets the D-Bus error object with error code and error message after which
// logs the error message will also be logged.
void LogAndSetError(brillo::ErrorPtr* err,
                    const string& code,
                    const string& msg) {
  if (err)
    *err = brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain, code,
                                 msg);
  LOG(ERROR) << msg;
}

}  // namespace

DlcService::DlcService(
    unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    unique_ptr<BootSlot> boot_slot,
    const FilePath& manifest_dir,
    const FilePath& preloaded_content_dir,
    const FilePath& content_dir,
    const FilePath& metadata_dir)
    : update_engine_proxy_(std::move(update_engine_proxy)),
      scheduled_period_ue_check_id_(MessageLoop::kTaskIdNull),
      weak_ptr_factory_(this) {
  dlc_manager_ = std::make_unique<DlcManager>(
      std::move(image_loader_proxy), std::move(boot_slot), manifest_dir,
      preloaded_content_dir, content_dir, metadata_dir);

  // Register D-Bus signal callbacks.
  update_engine_proxy_->RegisterStatusUpdateAdvancedSignalHandler(
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignalConnected,
                 weak_ptr_factory_.GetWeakPtr()));
}

DlcService::~DlcService() {
  if (scheduled_period_ue_check_id_ != MessageLoop::kTaskIdNull &&
      !brillo::MessageLoop::current()->CancelTask(
          scheduled_period_ue_check_id_))
    LOG(ERROR)
        << "Failed to cancel delayed update_engine check during cleanup.";
}

void DlcService::LoadDlcModuleImages() {
  dlc_manager_->LoadDlcModuleImages();
}

bool DlcService::Install(const DlcModuleList& dlc_module_list_in,
                         brillo::ErrorPtr* err) {
  // If an install is already in progress, dlcservice is busy.
  if (dlc_manager_->IsInstalling()) {
    LogAndSetError(err, kErrorBusy, "Another install is already in progress.");
    return false;
  }

  string err_code, err_msg;
  if (!dlc_manager_->InitInstall(dlc_module_list_in, &err_code, &err_msg)) {
    LogAndSetError(err, err_code, err_msg);
    return false;
  }

  // This is the unique DLC(s) that actually need to be installed.
  DlcModuleList unique_dlc_module_list_to_install =
      dlc_manager_->GetMissingInstalls();
  // Copy over the Omaha URL.
  unique_dlc_module_list_to_install.set_omaha_url(
      dlc_module_list_in.omaha_url());

  // Check if there is nothing to install.
  if (unique_dlc_module_list_to_install.dlc_module_infos_size() == 0) {
    DlcModuleList dlc_module_list;
    dlc_manager_->FinishInstall(&dlc_module_list, &err_code, &err_msg);
    InstallStatus install_status =
        CreateInstallStatus(Status::COMPLETED, kErrorNone, dlc_module_list, 1.);
    SendOnInstallStatusSignal(install_status);
    return true;
  }

  const auto CancelInstall = [this] {
    string throwaway_err_code, throwaway_err_msg;
    if (!dlc_manager_->CancelInstall(&throwaway_err_code, &throwaway_err_msg))
      LOG(ERROR) << throwaway_err_code << ":" << throwaway_err_msg;
  };
  Operation update_engine_operation;
  if (!GetUpdateEngineStatus(&update_engine_operation)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to get the status of Update Engine.");
    CancelInstall();
    return false;
  }
  switch (update_engine_operation) {
    case update_engine::UPDATED_NEED_REBOOT:
      LogAndSetError(err, kErrorNeedReboot,
                     "Update Engine applied update, device needs a reboot.");
      CancelInstall();
      return false;
    case update_engine::IDLE:
      break;
    default:
      LogAndSetError(err, kErrorBusy,
                     "Update Engine is performing operations.");
      CancelInstall();
      return false;
  }

  // Invokes update_engine to install the DLC module.
  if (!update_engine_proxy_->AttemptInstall(unique_dlc_module_list_to_install,
                                            nullptr)) {
    // TODO(kimjae): need update engine to propagate correct error message by
    // passing in |ErrorPtr| and being set within update engine, current default
    // is to indicate that update engine is updating because there is no way an
    // install should have taken place if not through dlcservice. (could also be
    // the case that an update applied between the time of the last status check
    // above, but just return |kErrorBusy| because the next time around if an
    // update has been applied and is in a reboot needed state, it will indicate
    // correctly then).
    LogAndSetError(err, kErrorBusy,
                   "Update Engine failed to schedule install operations.");
    CancelInstall();
    return false;
  }

  SchedulePeriodicInstallCheck(true);
  return true;
}

bool DlcService::Uninstall(const string& id_in, brillo::ErrorPtr* err) {
  Operation update_engine_operation;
  if (!GetUpdateEngineStatus(&update_engine_operation)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to get the status of Update Engine");
    return false;
  }
  switch (update_engine_operation) {
    case update_engine::IDLE:
    case update_engine::UPDATED_NEED_REBOOT:
      break;
    default:
      LogAndSetError(err, kErrorBusy, "Install or update is in progress.");
      return false;
  }

  // This is a weird case meaning update_engine was restarted and requires
  // cleanup of DLC(s) that were previously being thought to have been being
  // installed.
  if (dlc_manager_->IsInstalling())
    SendFailedSignalAndCleanup();

  string err_code, err_msg;
  if (!dlc_manager_->Delete(id_in, &err_code, &err_msg)) {
    LogAndSetError(err, err_code, err_msg);
    return false;
  }
  return true;
}

bool DlcService::GetInstalled(DlcModuleList* dlc_module_list_out,
                              brillo::ErrorPtr* err) {
  *dlc_module_list_out = dlc_manager_->GetInstalled();
  return true;
}

void DlcService::SendFailedSignalAndCleanup() {
  SendOnInstallStatusSignal(
      CreateInstallStatus(Status::FAILED, kErrorInternal, {}, 0.));
  string err_code, err_msg;
  if (!dlc_manager_->CancelInstall(&err_code, &err_msg))
    LogAndSetError(nullptr, err_code, err_msg);
}

void DlcService::PeriodicInstallCheck() {
  if (scheduled_period_ue_check_id_ == MessageLoop::kTaskIdNull) {
    LOG(ERROR) << "Should not have been called unless scheduled.";
    return;
  }

  scheduled_period_ue_check_id_ = MessageLoop::kTaskIdNull;

  if (!dlc_manager_->IsInstalling()) {
    LOG(ERROR) << "Should not have to check update_engine status while not "
                  "performing an install.";
    return;
  }

  Operation update_engine_operation;
  if (!GetUpdateEngineStatus(&update_engine_operation)) {
    LOG(ERROR)
        << "Failed to get the status of update_engine, it is most likely down.";
    SendFailedSignalAndCleanup();
    return;
  }
  switch (update_engine_operation) {
    case update_engine::UPDATED_NEED_REBOOT:
      LOG(ERROR) << "Thought to be installing DLC(s), but update_engine is not "
                    "installing and actually performed an update.";
      SendFailedSignalAndCleanup();
      break;
    case update_engine::IDLE:
      if (scheduled_period_ue_check_retry_) {
        LOG(INFO) << "Going to retry periodic check to check install signal.";
        SchedulePeriodicInstallCheck(false);
        return;
      }
      SendFailedSignalAndCleanup();
      break;
    default:
      SchedulePeriodicInstallCheck(true);
  }
}

void DlcService::SchedulePeriodicInstallCheck(bool retry) {
  if (scheduled_period_ue_check_id_ != MessageLoop::kTaskIdNull) {
    LOG(ERROR) << "Scheduling logic is internally not handled correctly, this "
               << "requires a scheduling logic update.";
    if (!brillo::MessageLoop::current()->CancelTask(
            scheduled_period_ue_check_id_)) {
      LOG(ERROR) << "Failed to cancel previous delayed update_engine check "
                 << "when scheduling.";
    }
  }
  scheduled_period_ue_check_id_ =
      brillo::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&DlcService::PeriodicInstallCheck,
                     weak_ptr_factory_.GetWeakPtr()),
          base::TimeDelta::FromSeconds(kUECheckTimeout));
  scheduled_period_ue_check_retry_ = retry;
}

bool DlcService::HandleStatusResult(const StatusResult& status_result) {
  // If we are not installing any DLC(s), no need to even handle status result.
  if (!dlc_manager_->IsInstalling())
    return false;

  // When a signal is received from update_engine, it is more efficient to
  // cancel the periodic check that's scheduled by re-posting a delayed task
  // after cancelling the currently set periodic check. If the cancelling of the
  // periodic check fails, let it run as it will be rescheduled correctly within
  // the periodic check itself again.
  if (!brillo::MessageLoop::current()->CancelTask(
          scheduled_period_ue_check_id_)) {
    LOG(ERROR) << "Failed to cancel delayed update_engine check when signal "
                  "was received from update_engine, so letting it run.";
  } else {
    scheduled_period_ue_check_id_ = MessageLoop::kTaskIdNull;
  }

  // This situation is reached if update_engine crashes during an install and
  // dlcservice still believes that it is waiting for an install to complete.
  // TODO(kimjae): Need to handle checking preiodically if an install is started
  // and dlcservice hasn't gotten an install progress in a certain period of
  // time, try to explicitly check update_engine's status and act accordingly.
  if (!status_result.is_install()) {
    int last_attempt_error;
    update_engine_proxy_->GetLastAttemptError(&last_attempt_error, nullptr);
    LOG(ERROR) << "Signal from update_engine indicates non-install, so install "
               << " failed and update_engine error code is: "
               << "(" << last_attempt_error << ")";
    SendFailedSignalAndCleanup();
    return false;
  }

  switch (status_result.current_operation()) {
    case Operation::IDLE:
      LOG(INFO)
          << "Signal from update_engine, proceeding to complete installation.";
      return true;
    case Operation::REPORTING_ERROR_EVENT:
      LOG(ERROR) << "Signal from update_engine indicates reporting failure.";
      SendFailedSignalAndCleanup();
      return false;
    // Only when update_engine's |Operation::DOWNLOADING| should dlcservice send
    // a signal out for |InstallStatus| for |Status::RUNNING|. Majority of the
    // install process for DLC(s) is during |Operation::DOWNLOADING|, this also
    // means that only a single growth from 0.0 to 1.0 for progress reporting
    // will happen.
    case Operation::DOWNLOADING:
      SendOnInstallStatusSignal(CreateInstallStatus(
          Status::RUNNING, kErrorNone, {}, status_result.progress()));
      FALLTHROUGH;
    default:
      SchedulePeriodicInstallCheck(true);
      return false;
  }
}

bool DlcService::GetUpdateEngineStatus(Operation* operation) {
  StatusResult status_result;
  if (!update_engine_proxy_->GetStatusAdvanced(&status_result, nullptr)) {
    return false;
  }
  *operation = status_result.current_operation();
  return true;
}

void DlcService::AddObserver(DlcService::Observer* observer) {
  observers_.push_back(observer);
}

void DlcService::SendOnInstallStatusSignal(
    const InstallStatus& install_status) {
  for (const auto& observer : observers_) {
    observer->SendInstallStatus(install_status);
  }
}

void DlcService::OnStatusUpdateAdvancedSignal(
    const StatusResult& status_result) {
  if (!HandleStatusResult(status_result))
    return;

  string err_code, err_msg;
  DlcModuleList dlc_module_list;
  if (!dlc_manager_->FinishInstall(&dlc_module_list, &err_code, &err_msg)) {
    LogAndSetError(nullptr, err_code, err_msg);
    InstallStatus install_status = CreateInstallStatus(
        Status::FAILED, kErrorInternal, dlc_module_list, 0.);
    SendOnInstallStatusSignal(install_status);
    return;
  }

  InstallStatus install_status =
      CreateInstallStatus(Status::COMPLETED, kErrorNone, dlc_module_list, 1.);
  SendOnInstallStatusSignal(install_status);
}

void DlcService::OnStatusUpdateAdvancedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to update_engine's StatusUpdate signal.";
  }
}

}  // namespace dlcservice
