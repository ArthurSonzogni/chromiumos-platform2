// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_status_utils.h"

#if BASE_VER < 822064
#include <base/logging.h>
#else
#include <base/notreached.h>
#endif
#include <base/strings/string_number_conversions.h>
#include <brillo/key_value_store.h>
#include <update_engine/dbus-constants.h>

#include "update_engine/common/error_code_utils.h"
#include "update_engine/cros/omaha_utils.h"

using brillo::KeyValueStore;
using std::string;
using update_engine::UpdateEngineStatus;
using update_engine::UpdateStatus;

namespace chromeos_update_engine {

namespace {

// Note: Do not change these, autotest depends on these string variables being
// exactly these matches.
const char kCurrentOp[] = "CURRENT_OP";
const char kIsInstall[] = "IS_INSTALL";
const char kIsEnterpriseRollback[] = "IS_ENTERPRISE_ROLLBACK";
const char kLastCheckedTime[] = "LAST_CHECKED_TIME";
const char kNewSize[] = "NEW_SIZE";
const char kNewVersion[] = "NEW_VERSION";
const char kProgress[] = "PROGRESS";
const char kWillPowerwashAfterReboot[] = "WILL_POWERWASH_AFTER_REBOOT";
const char kLastAttemptError[] = "LAST_ATTEMPT_ERROR";
const char kIsInteractive[] = "IS_INTERACTIVE";
const char kWillDeferUpdate[] = "WILL_DEFER_UPDATE";
const char kEolDate[] = "EOL_DATE";
const char kExtendedDate[] = "EXTENDED_DATE";
const char kExtendedOptInRequired[] = "EXTENDED_OPT_IN_REQUIRED";

}  // namespace

const char* UpdateStatusToString(const UpdateStatus& status) {
  switch (status) {
    case UpdateStatus::IDLE:
      return update_engine::kUpdateStatusIdle;
    case UpdateStatus::CHECKING_FOR_UPDATE:
      return update_engine::kUpdateStatusCheckingForUpdate;
    case UpdateStatus::UPDATE_AVAILABLE:
      return update_engine::kUpdateStatusUpdateAvailable;
    case UpdateStatus::NEED_PERMISSION_TO_UPDATE:
      return update_engine::kUpdateStatusNeedPermissionToUpdate;
    case UpdateStatus::DOWNLOADING:
      return update_engine::kUpdateStatusDownloading;
    case UpdateStatus::VERIFYING:
      return update_engine::kUpdateStatusVerifying;
    case UpdateStatus::FINALIZING:
      return update_engine::kUpdateStatusFinalizing;
    case UpdateStatus::UPDATED_NEED_REBOOT:
      return update_engine::kUpdateStatusUpdatedNeedReboot;
    case UpdateStatus::REPORTING_ERROR_EVENT:
      return update_engine::kUpdateStatusReportingErrorEvent;
    case UpdateStatus::ATTEMPTING_ROLLBACK:
      return update_engine::kUpdateStatusAttemptingRollback;
    case UpdateStatus::DISABLED:
      return update_engine::kUpdateStatusDisabled;
    case UpdateStatus::CLEANUP_PREVIOUS_UPDATE:
      return update_engine::kUpdateStatusCleanupPreviousUpdate;
    case UpdateStatus::UPDATED_BUT_DEFERRED:
      return update_engine::kUpdateStatusUpdatedButDeferred;
  }

  NOTREACHED_IN_MIGRATION();
  return nullptr;
}

string UpdateEngineStatusToString(const UpdateEngineStatus& status) {
  KeyValueStore key_value_store;

  key_value_store.SetString(kLastCheckedTime,
                            base::NumberToString(status.last_checked_time));
  key_value_store.SetString(kProgress, base::NumberToString(status.progress));
  key_value_store.SetString(kNewSize,
                            base::NumberToString(status.new_size_bytes));
  key_value_store.SetString(kCurrentOp, UpdateStatusToString(status.status));
  key_value_store.SetString(kNewVersion, status.new_version);
  key_value_store.SetBoolean(kIsEnterpriseRollback,
                             status.is_enterprise_rollback);
  key_value_store.SetBoolean(kIsInstall, status.is_install);
  key_value_store.SetBoolean(kWillPowerwashAfterReboot,
                             status.will_powerwash_after_reboot);
  key_value_store.SetString(kLastAttemptError,
                            utils::ErrorCodeToString(static_cast<ErrorCode>(
                                status.last_attempt_error)));
  key_value_store.SetBoolean(kIsInteractive, status.is_interactive);
  key_value_store.SetBoolean(kWillDeferUpdate, status.will_defer_update);
  key_value_store.SetString(kEolDate, base::NumberToString(status.eol_date));
  key_value_store.SetString(kExtendedDate,
                            base::NumberToString(status.extended_date));
  key_value_store.SetBoolean(kExtendedOptInRequired,
                             status.extended_opt_in_required);

  return key_value_store.SaveToString();
}

}  // namespace chromeos_update_engine
