// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/types/expected.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "login_manager/arc_sideload_status_interface.h"
#include "login_manager/dbus_util.h"
#include "login_manager/init_daemon_controller.h"
#include "login_manager/system_utils.h"
#include "login_manager/validator_utils.h"

namespace login_manager {
namespace {

constexpr char kLoggedInFlag[] = "/run/session_manager/logged_in";

// Because the cheets logs are huge, we set the D-Bus timeout to 1 minute.
const base::TimeDelta kBackupArcBugReportTimeout = base::Minutes(1);

#if USE_CHEETS
// To set the CPU limits of the Android container.
constexpr char kCpuSharesFile[] =
    "/sys/fs/cgroup/cpu/session_manager_containers/cpu.shares";
constexpr unsigned int kCpuSharesForeground = 1024;
constexpr unsigned int kCpuSharesBackground = 64;
#endif

}  // namespace

ArcManager::ArcManager(
    std::unique_ptr<Delegate> delegate,
    SystemUtils& system_utils,
    std::unique_ptr<InitDaemonController> init_controller,
    std::unique_ptr<ArcSideloadStatusInterface> arc_sideload_status,
    dbus::ObjectProxy* debugd_proxy)
    : delegate_(std::move(delegate)),
      system_utils_(system_utils),
      init_controller_(std::move(init_controller)),
      arc_sideload_status_(std::move(arc_sideload_status)),
      debugd_proxy_(debugd_proxy) {}

ArcManager::~ArcManager() = default;

void ArcManager::Initialize() {
  arc_sideload_status_->Initialize();
}

void ArcManager::Finalize() {
  arc_sideload_status_.reset();
}

bool ArcManager::IsAdbSideloadAllowed() const {
  return arc_sideload_status_->IsAdbSideloadAllowed();
}

void ArcManager::OnUpgradeArcContainer() {
  // |arc_start_time_| is initialized when the container is upgraded (rather
  // than when the mini-container starts) since we are interested in measuring
  // time from when the user logs in until the system is ready to be interacted
  // with.
  arc_start_time_ = base::TimeTicks::Now();
}

bool ArcManager::SetArcCpuRestriction(brillo::ErrorPtr* error,
                                      uint32_t in_restriction_state) {
#if USE_CHEETS
  std::string shares_out;
  switch (static_cast<ContainerCpuRestrictionState>(in_restriction_state)) {
    case CONTAINER_CPU_RESTRICTION_FOREGROUND:
      shares_out = std::to_string(kCpuSharesForeground);
      break;
    case CONTAINER_CPU_RESTRICTION_BACKGROUND:
      shares_out = std::to_string(kCpuSharesBackground);
      break;
    default:
      *error = CREATE_ERROR_AND_LOG(dbus_error::kArcCpuCgroupFail,
                                    "Invalid CPU restriction state specified.");
      return false;
  }
  if (!base::WriteFile(base::FilePath(kCpuSharesFile), shares_out)) {
    *error =
        CREATE_ERROR_AND_LOG(dbus_error::kArcCpuCgroupFail,
                             "Error updating Android container's cgroups.");
    return false;
  }
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif
}

bool ArcManager::EmitArcBooted(brillo::ErrorPtr* error,
                               const std::string& in_account_id) {
#if USE_CHEETS
  std::vector<std::string> env_vars;
  if (!in_account_id.empty()) {
    std::string actual_account_id;
    if (!ValidateAccountId(in_account_id, &actual_account_id)) {
      // TODO(alemate): adjust this error message after ChromeOS will stop using
      // email as cryptohome identifier.
      *error = CREATE_ERROR_AND_LOG(
          dbus_error::kInvalidAccount,
          "Provided email address is not valid.  ASCII only.");
      return false;
    }
    env_vars.emplace_back("CHROMEOS_USER=" + actual_account_id);
  }

  init_controller_->TriggerImpulse(kArcBootedImpulse, env_vars,
                                   InitDaemonController::TriggerMode::ASYNC);
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif
}

bool ArcManager::GetArcStartTimeTicks(brillo::ErrorPtr* error,
                                      int64_t* out_start_time) {
#if USE_CHEETS
  if (arc_start_time_.is_null()) {
    *error = CreateError(dbus_error::kNotStarted, "ARC is not started yet.");
    return false;
  }
  *out_start_time = arc_start_time_.ToInternalValue();
  return true;
#else
  *error = CreateError(dbus_error::kNotAvailable, "ARC not supported.");
  return false;
#endif  // !USE_CHEETS
}

void ArcManager::EnableAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  if (system_utils_->Exists(base::FilePath(kLoggedInFlag))) {
    auto error = CREATE_ERROR_AND_LOG(dbus_error::kSessionExists,
                                      "EnableAdbSideload is not allowed "
                                      "once a user logged in this boot.");
    response->ReplyWithError(error.get());
    return;
  }

  arc_sideload_status_->EnableAdbSideload(
      base::BindOnce(&ArcManager::EnableAdbSideloadCallbackAdaptor,
                     weak_factory_.GetWeakPtr(), std::move(response)));
}

void ArcManager::QueryAdbSideload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response) {
  arc_sideload_status_->QueryAdbSideload(
      base::BindOnce(&ArcManager::QueryAdbSideloadCallbackAdaptor,
                     weak_factory_.GetWeakPtr(), std::move(response)));
}

void ArcManager::EnableAdbSideloadCallbackAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    ArcSideloadStatusInterface::Status status,
    const char* error) {
  if (error != nullptr) {
    brillo::ErrorPtr dbus_error = CreateError(DBUS_ERROR_FAILED, error);
    response->ReplyWithError(dbus_error.get());
    return;
  }

  if (status == ArcSideloadStatusInterface::Status::NEED_POWERWASH) {
    brillo::ErrorPtr dbus_error = CreateError(DBUS_ERROR_NOT_SUPPORTED, error);
    response->ReplyWithError(dbus_error.get());
    return;
  }

  response->Return(status == ArcSideloadStatusInterface::Status::ENABLED);
}

void ArcManager::QueryAdbSideloadCallbackAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    ArcSideloadStatusInterface::Status status) {
  if (status == ArcSideloadStatusInterface::Status::NEED_POWERWASH) {
    brillo::ErrorPtr dbus_error =
        CreateError(DBUS_ERROR_NOT_SUPPORTED, "Need powerwash");
    response->ReplyWithError(dbus_error.get());
    return;
  }

  response->Return(status == ArcSideloadStatusInterface::Status::ENABLED);
}

void ArcManager::BackupArcBugReport(const std::string& account_id) {
  if (!delegate_->HasSession(account_id)) {
    LOG(ERROR) << "Cannot back up ARC bug report for inactive user.";
    return;
  }

  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               debugd::kBackupArcBugReport);
  dbus::MessageWriter writer(&method_call);

  writer.AppendString(account_id);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response(
      debugd_proxy_->CallMethodAndBlock(
          &method_call, kBackupArcBugReportTimeout.InMilliseconds()));

  if (!response.has_value() || !response.value()) {
    LOG(ERROR) << "Error contacting debugd to back up ARC bug report.";
  }
}

void ArcManager::DeleteArcBugReportBackup(const std::string& account_id) {
  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               debugd::kDeleteArcBugReportBackup);
  dbus::MessageWriter writer(&method_call);

  writer.AppendString(account_id);

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response(
      debugd_proxy_->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));

  if (!response.has_value() || !response.value()) {
    LOG(ERROR) << "Error contacting debugd to delete ARC bug report backup.";
  }
}

}  // namespace login_manager
