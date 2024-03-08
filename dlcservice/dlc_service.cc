// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_service.h"

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/files/file_util.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <constants/imageloader.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <lvmd/proto_bindings/lvmd.pb.h>

#include "dlcservice/error.h"
#include "dlcservice/system_state.h"
#include "dlcservice/types.h"
#include "dlcservice/utils.h"
#include "dlcservice/utils/utils_interface.h"

using brillo::ErrorPtr;
using brillo::MessageLoop;
using std::string;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {
// This value represents the delay in seconds between each idle installation
// status task.
constexpr size_t kPeriodicInstallCheckSecondsDelay = 10;

// This value here is the tolerance cap (allowance) of non-install signals
// broadcasted by `update_engine`. Keep in mind when changing of it's relation
// with the periodic install check delay as that will also determine the max
// idle period before an installation of a DLC is halted.
constexpr size_t kToleranceCap = 30;

DlcIdList ToDlcIdList(const DlcMap& dlcs,
                      const std::function<bool(const DlcType&)>& filter) {
  DlcIdList list;
  for (const auto& pair : dlcs) {
    if (filter(pair.second))
      list.push_back(pair.first);
  }
  return list;
}
}  // namespace

DlcService::DlcService(std::unique_ptr<DlcCreatorInterface> dlc_creator,
                       std::shared_ptr<UtilsInterface> utils)
    : periodic_install_check_id_(MessageLoop::kTaskIdNull),
      dlc_creator_(std::move(dlc_creator)),
      utils_(utils),
      weak_ptr_factory_(this) {}

DlcService::~DlcService() {
  if (periodic_install_check_id_ != MessageLoop::kTaskIdNull &&
      !brillo::MessageLoop::current()->CancelTask(periodic_install_check_id_))
    LOG(ERROR)
        << AlertLogTag(kCategoryCleanup)
        << "Failed to cancel delayed update_engine check during cleanup.";
}

void DlcService::Initialize() {
  auto* system_state = SystemState::Get();
  const auto prefs_dir = system_state->dlc_prefs_dir();
  if (!base::PathExists(prefs_dir)) {
    CHECK(CreateDir(prefs_dir))
        << "Failed to create dlc prefs directory: " << prefs_dir;
  }

  // Register D-Bus signal callbacks.
  auto* update_engine = system_state->update_engine();
  update_engine->RegisterStatusUpdateAdvancedSignalHandler(
      base::BindRepeating(&DlcService::OnStatusUpdateAdvancedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&DlcService::OnStatusUpdateAdvancedSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));

  system_state->installer()->OnReady(base::BindOnce(
      &DlcService::OnReadyInstaller, weak_ptr_factory_.GetWeakPtr()));

  supported_.clear();
  auto initialize_dlc = [this](const DlcId& id) -> void {
    auto result = supported_.emplace(id, dlc_creator_->Create(id));
    if (!result.first->second->Initialize()) {
      LOG(ERROR) << "Failed to initialize DLC " << id;
      supported_.erase(id);
    }
  };

  // Get supported DLCs from compressed metadata, and initialize them.
  for (const auto& id :
       utils_->GetSupportedDlcIds(SystemState::Get()->manifest_dir())) {
    initialize_dlc(id);
  }

  // Initialize supported DLC(s).
  for (const auto& id : ScanDirectory(SystemState::Get()->manifest_dir())) {
    if (supported_.find(id) == supported_.end())
      initialize_dlc(id);
  }
  CleanupUnsupported();
}

void DlcService::CleanupUnsupported() {
  auto* system_state = SystemState::Get();
  // Delete deprecated DLC(s) in content directory.
  for (const auto& id : ScanDirectory(system_state->content_dir())) {
    brillo::ErrorPtr tmp_err;
    if (GetDlc(id, &tmp_err) != nullptr)
      continue;
    for (const auto& path : GetPathsToDelete(id))
      if (base::PathExists(path)) {
        if (!brillo::DeletePathRecursively(path))
          PLOG(ERROR) << "Failed to delete path=" << path;
        else
          LOG(INFO) << "Deleted path=" << path << " for deprecated DLC=" << id;
      }
  }

#if USE_LVM_STATEFUL_PARTITION
  CleanupUnsupportedLvs();
#endif  // USE_LVM_STATEFUL_PARTITION

  // Delete the unsupported/preload not allowed DLC(s) in the preloaded
  // directory.
  auto preloaded_content_dir = system_state->preloaded_content_dir();
  for (const auto& id : ScanDirectory(preloaded_content_dir)) {
    brillo::ErrorPtr tmp_err;
    auto* dlc = GetDlc(id, &tmp_err);
    if (dlc != nullptr && dlc->IsPreloadAllowed())
      continue;

    // Preloading is not allowed for this image so it will be deleted.
    auto path = JoinPaths(preloaded_content_dir, id);
    if (!brillo::DeletePathRecursively(path))
      PLOG(ERROR) << "Failed to delete path=" << path;
    else
      LOG(INFO) << "Deleted path=" << path
                << " for unsupported/preload not allowed DLC=" << id;
  }
}

#if USE_LVM_STATEFUL_PARTITION
void DlcService::CleanupUnsupportedLvs() {
  lvmd::LogicalVolumeList lvs;
  if (!SystemState::Get()->lvmd_wrapper()->ListLogicalVolumes(&lvs)) {
    LOG(ERROR) << "Failed to list logical volumes for cleaning.";
  } else {
    std::vector<string> lv_names;
    for (const auto& lv : lvs.logical_volume()) {
      const auto& id = utils_->LogicalVolumeNameToId(lv.name());
      if (id.empty()) {
        continue;
      }
      brillo::ErrorPtr tmp_err;
      if (GetDlc(id, &tmp_err) != nullptr) {
        continue;
      }
      lv_names.push_back(lv.name());
    }
    // Asynchronously delete all unsupported DLC(s).
    if (!lv_names.empty()) {
      SystemState::Get()->lvmd_wrapper()->RemoveLogicalVolumesAsync(
          lv_names,
          base::BindOnce(
              [](const decltype(lv_names)& lv_names, bool success) {
                if (success) {
                  LOG(INFO)
                      << "Successfully removed all stale logical volumes.";
                  return;
                }
                LOG(ERROR) << "Failed to remove stale logical volumes: "
                           << base::JoinString(lv_names, ", ");
              },
              lv_names));
    }
  }
}
#endif  // USE_LVM_STATEFUL_PARTITION

void DlcService::OnReadyInstaller(bool available) {
  LOG(INFO) << "Installer service available=" << available;
  GetUpdateEngineStatusAsync();
}

void DlcService::Install(
    const InstallRequest& install_request,
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  // TODO(b/220202911): Start parallelizing installations.
  // Ash Chrome dlcservice client handled installations in a queue, but
  // dlcservice has numerous other DBus clients that can all race to install
  // various DLCs. This checks here need to guarantee atomic installation per
  // DLC in sequence.
  auto ret_func =
      [install_request](
          std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
          brillo::ErrorPtr* err) -> void {
    // Only send error metrics in here. Install success metrics is sent in
    // |DlcBase|.
    LOG(ERROR) << AlertLogTag(kCategoryInstall)
               << "Failed to install DLC=" << install_request.id();
    SystemState::Get()->metrics()->SendInstallResultFailure(err);
    Error::ConvertToDbusError(err);
    std::move(response)->ReplyWithError(err->get());
  };

  brillo::ErrorPtr err;
  // Try to install and figure out if install through update_engine is needed.
  bool external_install_needed = false;
  auto manager_install = [this](const InstallRequest& install_request,
                                bool* external_install_needed, ErrorPtr* err) {
    DCHECK(err);
    const auto id = install_request.id();
    auto* dlc = GetDlc(id, err);
    if (dlc == nullptr) {
      return false;
    }

    dlc->SetReserve(install_request.reserve());

    // If the DLC is being installed, nothing can be done anymore.
    if (dlc->IsInstalling()) {
      return true;
    }

    // Otherwise proceed to install the DLC.
    if (!dlc->Install(err)) {
      Error::AddInternalTo(
          err, FROM_HERE, error::kFailedInternal,
          base::StringPrintf("Failed to initialize installation for DLC=%s",
                             id.c_str()));
      return false;
    }

    // If the DLC is now in installing state, it means it now needs
    // update_engine installation.
    *external_install_needed = dlc->IsInstalling();
    return true;
  };
  if (!manager_install(install_request, &external_install_needed, &err)) {
    LOG(ERROR) << "Failed to install DLC=" << install_request.id();
    return ret_func(std::move(response), &err);
  }

  // Install through update_engine only if needed.
  if (!external_install_needed) {
    std::move(response)->Return();
    return;
  }

  const auto& id = install_request.id();
  if (installing_dlc_id_ && installing_dlc_id_ != id) {
    auto err_str = base::StringPrintf(
        "Installation already in progress for (%s), can't install %s right "
        "now.",
        installing_dlc_id_.value().c_str(), id.c_str());
    LOG(ERROR) << err_str;
    err = Error::Create(FROM_HERE, kErrorBusy, err_str);
    ErrorPtr tmp_err;
    auto manager_cancel = [this](const DlcId& id, const ErrorPtr& err_in,
                                 ErrorPtr* err) -> bool {
      DCHECK(err);
      auto* dlc = GetDlc(id, err);
      return dlc && (!dlc->IsInstalling() || dlc->CancelInstall(err_in, err));
    };
    if (!manager_cancel(id, err, &tmp_err))
      LOG(ERROR) << "Failed to cancel install for DLC=" << id;
    return ret_func(std::move(response), &err);
  }

  InstallViaInstaller(install_request, std::move(response));
}

void DlcService::InstallViaInstaller(
    const InstallRequest& install_request,
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  auto ret_func =
      [this, install_request](
          std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
          brillo::ErrorPtr err) -> void {
    LOG(ERROR) << AlertLogTag(kCategoryInstall)
               << "Failed to install DLC=" << install_request.id();
    SystemState::Get()->metrics()->SendInstallResultFailure(&err);
    Error::ConvertToDbusError(&err);
    // dlcservice must cancel the install as update_engine won't be able to
    // install the initialized DLC.
    CancelInstall(err);
    std::move(response)->ReplyWithError(err.get());
  };

  const auto id = install_request.id();
  // Need to set in order for the cancellation of DLC setup.
  installing_dlc_id_ = id;

  // If update_engine needs to handle the installation, wait for the service to
  // be up and DBus object exported. Returning busy error will allow Chrome
  // client to retry the installation.
  if (!SystemState::Get()->installer()->IsReady()) {
    string err_str = "Installation called before installer is available.";
    return ret_func(std::move(response),
                    Error::Create(FROM_HERE, kErrorBusy, err_str));
  }

  // Check what state update_engine is in.
  if (SystemState::Get()->update_engine_status().current_operation() ==
      update_engine::UPDATED_NEED_REBOOT) {
    return ret_func(
        std::move(response),
        Error::Create(FROM_HERE, kErrorNeedReboot,
                      "Update Engine applied update, device needs a reboot."));
  }

  LOG(INFO) << "Sending request to install DLC=" << id;
  brillo::ErrorPtr err;
  auto* dlc = GetDlc(id, &err);
  if (dlc == nullptr) {
    return ret_func(std::move(response), err->Clone());
  }
  // TODO(kimjae): need update engine to propagate correct error message by
  // passing in |ErrorPtr| and being set within update engine, current default
  // is to indicate that update engine is updating because there is no way an
  // install should have taken place if not through dlcservice. (could also be
  // the case that an update applied between the time of the last status check
  // above, but just return |kErrorBusy| because the next time around if an
  // update has been applied and is in a reboot needed state, it will indicate
  // correctly then).
  auto [response_bind1, response_bind2] =
      base::SplitOnceCallback(base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
             brillo::ErrorPtr err) -> void {
            if (err.get()) {
              std::move(response)->ReplyWithError(err.get());
            } else {
              std::move(response)->Return();
            }
          },
          std::move(response)));

  SystemState::Get()->installer()->Install(
      Installer::InstallArgs{
          .id = id,
          .url = install_request.omaha_url(),
          .scaled = dlc->IsScaled(),
          .force_ota = dlc->IsForceOTA() || install_request.force_ota(),
      },
      base::BindOnce(&DlcService::OnInstallSuccess,
                     weak_ptr_factory_.GetWeakPtr(), std::move(response_bind1)),
      base::BindOnce(&DlcService::OnInstallFailure,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(response_bind2)));
}

void DlcService::OnInstallSuccess(
    base::OnceCallback<void(brillo::ErrorPtr)> response_func) {
  // By now the update_engine is installing the DLC, so schedule a periodic
  // install checker in case we miss update_engine signals.
  SchedulePeriodicInstallCheck();
  std::move(response_func).Run(nullptr);
}

void DlcService::OnInstallFailure(
    base::OnceCallback<void(brillo::ErrorPtr)> response_func,
    brillo::Error* err) {
  // Handled during other signal/response.
  if (!installing_dlc_id_)
    return;
  // Keep this double logging until tagging is removed/updated.
  LOG(ERROR) << "Update Engine failed to install requested DLCs: "
             << (err ? Error::ToString(err->Clone())
                     : "Missing error from update engine proxy.");
  LOG(ERROR) << AlertLogTag(kCategoryInstall)
             << "Failed to install DLC=" << installing_dlc_id_.value();
  auto ret_err =
      Error::Create(FROM_HERE, kErrorBusy,
                    "Update Engine failed to schedule install operations.");
  // dlcservice must cancel the install as update_engine won't be able to
  // install the initialized DLC.
  CancelInstall(ret_err);
  SystemState::Get()->metrics()->SendInstallResultFailure(&ret_err);
  Error::ConvertToDbusError(&ret_err);
  std::move(response_func).Run(ret_err->Clone());
}

bool DlcService::Uninstall(const string& id, brillo::ErrorPtr* err) {
  auto manager_uninstall = [this](const DlcId& id, ErrorPtr* err) -> bool {
    DCHECK(err);
    // `GetDlc(...)` should set the error when `nullptr` is returned.
    auto* dlc = GetDlc(id, err);
    return dlc && dlc->Uninstall(err);
  };
  bool result = manager_uninstall(id, err);
  SystemState::Get()->metrics()->SendUninstallResult(err);
  if (!result) {
    LOG(ERROR) << AlertLogTag(kCategoryUninstall)
               << "Failed to uninstall DLC=" << id;
    Error::ConvertToDbusError(err);
  }
  return result;
}

bool DlcService::Deploy(const DlcId& id, brillo::ErrorPtr* err) {
  DCHECK(err);
  auto* dlc = GetDlc(id, err);
  return dlc && dlc->Deploy(err);
}

DlcInterface* DlcService::GetDlc(const DlcId& id, brillo::ErrorPtr* err) {
  const auto& iter = supported_.find(id);
  if (iter == supported_.end()) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Passed unsupported DLC=%s", id.c_str()));
    return nullptr;
  }
  return iter->second.get();
}

DlcIdList DlcService::GetInstalled() {
  return ToDlcIdList(supported_,
                     [](const DlcType& dlc) { return dlc->IsInstalled(); });
}

DlcIdList DlcService::GetExistingDlcs() {
  std::unordered_set<DlcId> unique_existing_dlcs;

  // This scans the files based DLC(s).
  for (const auto& id : ScanDirectory(SystemState::Get()->content_dir())) {
    brillo::ErrorPtr tmp_err;
    if (GetDlc(id, &tmp_err) != nullptr) {
      unique_existing_dlcs.insert(id);
    }
  }

#if USE_LVM_STATEFUL_PARTITION
  // This scans the logical volume based DLC(s).
  lvmd::LogicalVolumeList lvs;
  if (!SystemState::Get()->lvmd_wrapper()->ListLogicalVolumes(&lvs)) {
    LOG(ERROR) << "Failed to list logical volumes.";
  } else {
    for (const auto& lv : lvs.logical_volume()) {
      const auto& id = utils_->LogicalVolumeNameToId(lv.name());
      if (id.empty()) {
        continue;
      }
      brillo::ErrorPtr tmp_err;
      if (GetDlc(id, &tmp_err) != nullptr) {
        unique_existing_dlcs.insert(id);
      }
    }
  }
#endif  // USE_LVM_STATEFUL_PARTITION

  return {std::begin(unique_existing_dlcs), std::end(unique_existing_dlcs)};
}

DlcIdList DlcService::GetDlcsToUpdate() {
  return ToDlcIdList(
      supported_, [](const DlcType& dlc) { return dlc->MakeReadyForUpdate(); });
}

bool DlcService::InstallCompleted(const DlcIdList& ids, ErrorPtr* err) {
  auto manager_install_completed = [this](const DlcIdList& ids,
                                          brillo::ErrorPtr* err) {
    DCHECK(err);
    bool ret = true;
    for (const auto& id : ids) {
      auto* dlc = GetDlc(id, err);
      if (dlc == nullptr) {
        LOG(WARNING) << "Trying to complete installation for unsupported DLC="
                     << id;
        ret = false;
      } else if (!dlc->InstallCompleted(err)) {
        PLOG(WARNING) << "Failed to complete install.";
        ret = false;
      }
    }
    // The returned error pertains to the last error happened. We probably don't
    // need any accumulation of errors.
    return ret;
  };
  return manager_install_completed(ids, err);
}

bool DlcService::UpdateCompleted(const DlcIdList& ids, ErrorPtr* err) {
  auto manager_update_completed = [this](const DlcIdList& ids,
                                         brillo::ErrorPtr* err) {
    DCHECK(err);
    bool ret = true;
    for (const auto& id : ids) {
      auto* dlc = GetDlc(id, err);
      if (dlc == nullptr) {
        LOG(WARNING) << "Trying to complete update for unsupported DLC=" << id;
        ret = false;
      } else if (!dlc->UpdateCompleted(err)) {
        LOG(WARNING) << "Failed to complete update.";
        ret = false;
      }
    }
    // The returned error pertains to the last error happened. We probably don't
    // need any accumulation of errors.
    return ret;
  };
  return manager_update_completed(ids, err);
}

bool DlcService::FinishInstall(ErrorPtr* err) {
  if (!installing_dlc_id_) {
    LOG(ERROR) << "No DLC installation to finish.";
    return false;
  }
  auto id = installing_dlc_id_.value();
  installing_dlc_id_.reset();
  auto manager_finish_install = [this](const DlcId& id, ErrorPtr* err) {
    DCHECK(err);
    auto dlc = GetDlc(id, err);
    if (!dlc) {
      *err = Error::Create(FROM_HERE, kErrorInvalidDlc,
                           "Finishing installation for invalid DLC.");
      return false;
    }
    if (!dlc->IsInstalling()) {
      *err = Error::Create(
          FROM_HERE, kErrorInternal,
          "Finishing installation for a DLC that is not being installed.");
      return false;
    }
    return dlc->FinishInstall(/*installed_by_ue=*/true, err);
  };
  return manager_finish_install(id, err);
}

void DlcService::CancelInstall(const ErrorPtr& err_in) {
  if (!installing_dlc_id_) {
    LOG(ERROR) << "No DLC installation to cancel.";
    return;
  }

  auto manager_cancel = [this](const DlcId& id, const ErrorPtr& err_in,
                               ErrorPtr* err) -> bool {
    DCHECK(err);
    auto* dlc = GetDlc(id, err);
    return dlc && (!dlc->IsInstalling() || dlc->CancelInstall(err_in, err));
  };
  auto id = installing_dlc_id_.value();
  installing_dlc_id_.reset();
  ErrorPtr tmp_err;
  if (!manager_cancel(id, err_in, &tmp_err))
    LOG(ERROR) << "Failed to cancel install for DLC=" << id;
}

void DlcService::PeriodicInstallCheck() {
  periodic_install_check_id_ = MessageLoop::kTaskIdNull;

  // If we're not installing anything anymore, no need to schedule again.
  if (!installing_dlc_id_)
    return;

  constexpr base::TimeDelta kNotSeenStatusDelay =
      base::Seconds(kPeriodicInstallCheckSecondsDelay);
  auto* system_state = SystemState::Get();
  if ((system_state->clock()->Now() -
       system_state->update_engine_status_timestamp()) > kNotSeenStatusDelay) {
    GetUpdateEngineStatusAsync();
  }

  SchedulePeriodicInstallCheck();
}

void DlcService::SchedulePeriodicInstallCheck() {
  if (periodic_install_check_id_ != MessageLoop::kTaskIdNull) {
    LOG(INFO) << "Another periodic install check already scheduled.";
    return;
  }

  periodic_install_check_id_ = brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DlcService::PeriodicInstallCheck,
                     weak_ptr_factory_.GetWeakPtr()),
      kUECheckTimeout);
}

bool DlcService::HandleStatusResult(brillo::ErrorPtr* err) {
  // If we are not installing any DLC(s), no need to even handle status result.
  if (!installing_dlc_id_) {
    tolerance_count_ = 0;
    return true;
  }

  const StatusResult& status = SystemState::Get()->update_engine_status();
  if (!status.is_install()) {
    if (++tolerance_count_ <= kToleranceCap) {
      LOG(WARNING)
          << "Signal from update_engine indicates that it's not for an "
             "install, but dlcservice was waiting for an install.";
      return true;
    }
    tolerance_count_ = 0;
    *err = Error::CreateInternal(
        FROM_HERE, error::kFailedInstallInUpdateEngine,
        "Signal from update_engine indicates that it's not for an install, but "
        "dlcservice was waiting for an install.");
    CancelInstall(*err);
    SystemState::Get()->metrics()->SendInstallResultFailure(err);
    return false;
  }

  // Reset the tolerance if a valid status is handled.
  tolerance_count_ = 0;

  switch (status.current_operation()) {
    case update_engine::UPDATED_NEED_REBOOT:
      *err =
          Error::Create(FROM_HERE, kErrorNeedReboot,
                        "Update Engine applied update, device needs a reboot.");
      break;
    case Operation::IDLE:
      LOG(INFO)
          << "Signal from update_engine, proceeding to complete installation.";
      // Send metrics in |DlcBase::FinishInstall| and not here since we might
      // be executing this call for multiple DLCs.
      if (!FinishInstall(err)) {
        LOG(ERROR) << "Failed to finish install.";
        return false;
      }
      return true;
    case Operation::REPORTING_ERROR_EVENT:
      *err =
          Error::CreateInternal(FROM_HERE, error::kFailedInstallInUpdateEngine,
                                "update_engine indicates reporting failure.");
      break;
    // Only when update_engine's |Operation::DOWNLOADING| should the DLC send
    // |DlcState::INSTALLING|. Majority of the install process for DLC(s) is
    // during |Operation::DOWNLOADING|, this also means that only a single
    // growth from 0.0 to 1.0 for progress reporting will happen.
    case Operation::DOWNLOADING: {
      auto manager_change_progress = [this](double progress) {
        for (auto& pr : supported_) {
          auto& dlc = pr.second;
          if (dlc->IsInstalling()) {
            dlc->ChangeProgress(progress);
          }
        }
      };
      manager_change_progress(status.progress());

      [[fallthrough]];
    }
    default:
      return true;
  }

  CancelInstall(*err);
  SystemState::Get()->metrics()->SendInstallResultFailure(err);
  return false;
}

void DlcService::GetUpdateEngineStatusAsync() {
  SystemState::Get()->update_engine()->GetStatusAdvancedAsync(
      base::BindOnce(&DlcService::OnStatusUpdateAdvancedSignal,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&DlcService::OnGetUpdateEngineStatusAsyncError,
                     weak_ptr_factory_.GetWeakPtr()));
}

void DlcService::OnGetUpdateEngineStatusAsyncError(brillo::Error* err) {
  if (err) {
    auto err_ptr = err->Clone();
    LOG(ERROR) << "Failed to get update_engine status, err="
               << Error::ToString(err_ptr);
  }
}

void DlcService::OnStatusUpdateAdvancedSignal(
    const StatusResult& status_result) {
  // Always set the status.
  SystemState::Get()->set_update_engine_status(status_result);

  ErrorPtr err;
  if (!HandleStatusResult(&err))
    DCHECK(err.get());
}

void DlcService::OnStatusUpdateAdvancedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << AlertLogTag(kCategoryInit)
               << "Failed to connect to update_engine's StatusUpdate signal.";
  }
}

bool DlcService::Unload(const std::string& id, brillo::ErrorPtr* err) {
  auto* dlc = GetDlc(id, err);
  return dlc && dlc->Unload(err);
}

bool DlcService::Unload(const dlcservice::UnloadRequest::SelectDlc& select,
                        const base::FilePath& mount_base,
                        brillo::ErrorPtr* err) {
  if (!select.user_tied() && !select.scaled()) {
    LOG(WARNING) << "DLC selection is empty.";
    return true;
  }

  const auto& mounted = ScanDirectory(base::FilePath(mount_base));

  DlcIdList failed_ids;
  for (const auto& id : mounted) {
    auto* dlc = GetDlc(id, err);
    if (!dlc || !((select.user_tied() && dlc->IsUserTied()) ||
                  (select.scaled() && dlc->IsScaled()))) {
      continue;
    }

    ErrorPtr tmp_err;
    if (!dlc->Unload(&tmp_err)) {
      failed_ids.push_back(id);
    }
  }

  if (failed_ids.size()) {
    *err = Error::Create(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to unload DLCs: %s",
                           base::JoinString(failed_ids, ",").c_str()));
  }
  return failed_ids.empty();
}

}  // namespace dlcservice
