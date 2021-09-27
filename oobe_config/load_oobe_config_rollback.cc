// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_rollback.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_data.pb.h"

using base::FilePath;
using std::string;
using std::unique_ptr;

namespace oobe_config {

LoadOobeConfigRollback::LoadOobeConfigRollback(OobeConfig* oobe_config,
                                               bool allow_unencrypted)
    : oobe_config_(oobe_config), allow_unencrypted_(allow_unencrypted) {}

bool LoadOobeConfigRollback::GetOobeConfigJson(string* config,
                                               string* enrollment_domain) {
  LOG(INFO) << "Looking for rollback state.";

  *config = "";
  *enrollment_domain = "";

  // Precondition for running rollback.
  if (!oobe_config_->FileExists(kRestoreTempPath)) {
    LOG(ERROR) << "Restore destination path doesn't exist.";
    return false;
  }

  if (oobe_config_->ShouldRestoreRollbackData()) {
    LOG(INFO) << "Starting rollback restore.";

    // Decrypt the proto from kUnencryptedRollbackDataPath.
    bool restore_result;
    if (allow_unencrypted_) {
      restore_result = oobe_config_->UnencryptedRollbackRestore();
    } else {
      restore_result = oobe_config_->EncryptedRollbackRestore();
    }

    if (!restore_result) {
      LOG(ERROR) << "Failed to restore rollback data";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage1Failure);
      return false;
    }

    // We load the proto from kEncryptedStatefulRollbackDataPath.
    string rollback_data_str;
    if (!oobe_config_->ReadFile(kEncryptedStatefulRollbackDataPath,
                                &rollback_data_str)) {
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }
    RollbackData rollback_data;
    if (!rollback_data.ParseFromString(rollback_data_str)) {
      LOG(ERROR) << "Couldn't parse proto.";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }
    // We get the data for Chrome and assemble the config.
    if (!AssembleConfig(rollback_data, config)) {
      LOG(ERROR) << "Failed to assemble config.";
      metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kStage3Failure);
      return false;
    }

    // If it succeeded, we remove all files from
    // kEncryptedStatefulRollbackDataPath.
    LOG(INFO) << "Cleaning up rollback data.";
    oobe_config_->CleanupEncryptedStatefulDirectory();

    LOG(INFO) << "Rollback restore completed successfully.";
    metrics_.RecordRestoreResult(Metrics::OobeRestoreResult::kSuccess);
    return true;
  }

  return false;
}

bool LoadOobeConfigRollback::AssembleConfig(const RollbackData& rollback_data,
                                            string* config) {
  // Possible values are defined in
  // chrome/browser/resources/chromeos/login/components/oobe_types.js.
  // TODO(zentaro): Export these strings as constants.
  base::Value dictionary(base::Value::Type::DICTIONARY);
  // Always skip next screen.
  dictionary.SetBoolKey("welcomeNext", true);
  // Always skip network selection screen if possible.
  dictionary.SetBoolKey("networkUseConnected", true);
  // We don't want updates after rolling back.
  dictionary.SetBoolKey("updateSkipNonCritical", true);
  // Set whether metrics should be enabled if it exists in |rollback_data|.
  dictionary.SetBoolKey("eulaSendStatistics",
                        rollback_data.eula_send_statistics());
  // Set whether the EULA as already accepted and can be skipped if the field is
  // present in |rollback_data|.
  dictionary.SetBoolKey("eulaAutoAccept", rollback_data.eula_auto_accept());
  // Tell Chrome that it still has to create some robot accounts that were
  // destroyed during rollback.
  dictionary.SetBoolKey("enrollmentRestoreAfterRollback", true);
  // Send network config to Chrome. Chrome takes care of how to reconfigure the
  // networks.
  dictionary.SetStringKey("networkConfig", rollback_data.network_config());

  return base::JSONWriter::Write(dictionary, config);
}

}  // namespace oobe_config
