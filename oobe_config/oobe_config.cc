// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config.h"

#include <map>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_data.pb.h"

namespace oobe_config {

OobeConfig::OobeConfig() = default;
OobeConfig::~OobeConfig() = default;

base::FilePath OobeConfig::GetPrefixedFilePath(
    const base::FilePath& file_path) const {
  if (prefix_path_for_testing_.empty())
    return file_path;
  DCHECK(!file_path.value().empty());
  DCHECK_EQ('/', file_path.value()[0]);
  return prefix_path_for_testing_.Append(file_path.value().substr(1));
}

bool OobeConfig::ReadFileWithoutPrefix(const base::FilePath& file_path,
                                       std::string* out_content) const {
  bool result = base::ReadFileToString(file_path, out_content);
  if (result) {
    LOG(INFO) << "Loaded " << file_path.value();
  } else {
    LOG(ERROR) << "Couldn't read " << file_path.value();
    *out_content = "";
  }
  return result;
}

bool OobeConfig::ReadFile(const base::FilePath& file_path,
                          std::string* out_content) const {
  return ReadFileWithoutPrefix(GetPrefixedFilePath(file_path), out_content);
}

bool OobeConfig::FileExists(const base::FilePath& file_path) const {
  return base::PathExists(GetPrefixedFilePath(file_path));
}

bool OobeConfig::WriteFileWithoutPrefix(const base::FilePath& file_path,
                                        const std::string& data) const {
  if (!base::CreateDirectory(file_path.DirName())) {
    PLOG(ERROR) << "Couldn't create directory for " << file_path.value();
    return false;
  }
  int bytes_written = base::WriteFile(file_path, data.c_str(), data.size());
  if (bytes_written != data.size()) {
    PLOG(ERROR) << "Couldn't write " << file_path.value()
                << " bytes=" << bytes_written;
    return false;
  }
  LOG(INFO) << "Wrote " << file_path.value();
  return true;
}

bool OobeConfig::WriteFile(const base::FilePath& file_path,
                           const std::string& data) const {
  return WriteFileWithoutPrefix(GetPrefixedFilePath(file_path), data);
}

void OobeConfig::GetRollbackData(RollbackData* rollback_data) const {
  if (base::PathExists(
          GetPrefixedFilePath(kSaveTempPath.Append(kOobeCompletedFileName)))) {
    // If OOBE has been completed already, we know the EULA has been accepted.
    rollback_data->set_eula_auto_accept(true);
  }

  if (base::PathExists(GetPrefixedFilePath(
          kSaveTempPath.Append(kMetricsReportingEnabledFileName)))) {
    // If |kMetricsReportingEnabledFile| exists, metrics are enabled.
    rollback_data->set_eula_send_statistics(true);
  }
  return;
}

bool OobeConfig::GetSerializedRollbackData(
    std::string* serialized_rollback_data) const {
  RollbackData rollback_data;
  GetRollbackData(&rollback_data);

  if (!rollback_data.SerializeToString(serialized_rollback_data)) {
    LOG(ERROR) << "Couldn't serialize proto.";
    return false;
  }

  return true;
}

bool OobeConfig::UnencryptedRollbackSave() const {
  std::string serialized_rollback_data;
  if (!GetSerializedRollbackData(&serialized_rollback_data)) {
    return false;
  }

  if (!WriteFile(kUnencryptedStatefulRollbackDataPath,
                 serialized_rollback_data)) {
    LOG(ERROR) << "Failed to write unencrypted rollback data file.";
    return false;
  }

  if (!WriteFile(kDataSavedFile, std::string())) {
    LOG(ERROR) << "Failed to write data saved flag.";
    return false;
  }

  return true;
}

bool OobeConfig::EncryptedRollbackSave() const {
  std::string serialized_rollback_data;
  if (!GetSerializedRollbackData(&serialized_rollback_data)) {
    return false;
  }

  // TODO(crbug/1212958) implement encryption that works across TPM clear.
  LOG(WARNING) << "Encryption not yet implemented!";
  std::string encrypted = serialized_rollback_data;

  LOG(INFO) << "Writing encrypted rollback data.";
  if (!WriteFile(kUnencryptedStatefulRollbackDataPath, encrypted)) {
    LOG(ERROR) << "Failed to write encrypted rollback data file.";
    return false;
  }

  if (!WriteFile(kDataSavedFile, std::string())) {
    LOG(ERROR) << "Failed to write data saved flag.";
    return false;
  }

  return true;
}

bool OobeConfig::UnencryptedRollbackRestore() const {
  std::string rollback_data_str;
  if (!ReadFile(kUnencryptedStatefulRollbackDataPath, &rollback_data_str)) {
    return false;
  }
  // Write the unencrypted data immediately to
  // kEncryptedStatefulRollbackDataPath.
  if (!WriteFile(kEncryptedStatefulRollbackDataPath, rollback_data_str)) {
    return false;
  }

  RollbackData rollback_data;
  if (!rollback_data.ParseFromString(rollback_data_str)) {
    LOG(ERROR) << "Couldn't parse proto.";
    return false;
  }
  LOG(INFO) << "Parsed " << kUnencryptedStatefulRollbackDataPath.value();

  return true;
}

bool OobeConfig::EncryptedRollbackRestore() const {
  std::string encrypted_data;
  if (!ReadFile(kUnencryptedStatefulRollbackDataPath, &encrypted_data)) {
    return false;
  }

  // Decrypt the data.
  LOG(INFO) << "Decrypting rollback data size=" << encrypted_data.size();
  LOG(WARNING) << "Encryption not yet implemented, expecting unencrypted data!";
  std::string rollback_data_str = encrypted_data;

  // Write the unencrypted data immediately to
  // kEncryptedStatefulRollbackDataPath.
  if (!WriteFile(kEncryptedStatefulRollbackDataPath, rollback_data_str)) {
    return false;
  }

  RollbackData rollback_data;
  if (!rollback_data.ParseFromString(rollback_data_str)) {
    LOG(ERROR) << "Couldn't parse proto.";
    return false;
  }
  LOG(INFO) << "Parsed " << kUnencryptedStatefulRollbackDataPath.value();

  return true;
}

void OobeConfig::CleanupEncryptedStatefulDirectory() const {
  base::FileEnumerator iter(
      GetPrefixedFilePath(kEncryptedStatefulRollbackDataPath), false,
      base::FileEnumerator::FILES);
  for (auto file = iter.Next(); !file.empty(); file = iter.Next()) {
    if (!base::DeleteFile(file)) {
      LOG(ERROR) << "Couldn't delete " << file.value();
    }
  }
}

bool OobeConfig::CheckFirstStage() const {
  // Check whether we're in the first stage.
  if (!FileExists(kUnencryptedStatefulRollbackDataPath)) {
    LOG(INFO) << "CheckFirstStage: Rollback data "
              << kUnencryptedStatefulRollbackDataPath.value()
              << " does not exist.";
    return false;
  }
  if (FileExists(kFirstStageCompletedFile)) {
    LOG(INFO) << "CheckFirstStage: First stage already completed.";
    return false;
  }

  // At this point, we should be in the first stage. We verify, that the other
  // files are in a consistent state.
  if (FileExists(kSecondStageCompletedFile)) {
    LOG(ERROR)
        << "CheckFirstStage: Second stage is completed but first stage is not.";
    return false;
  }
  if (FileExists(kEncryptedStatefulRollbackDataPath)) {
    LOG(ERROR) << "CheckFirstStage: Both encrypted and unencrypted rollback "
                  "data path exists.";
    return false;
  }

  LOG(INFO) << "CheckFirstStage: OK.";
  return true;
}

bool OobeConfig::CheckSecondStage() const {
  // Check whether we're in the second stage.
  if (!FileExists(kFirstStageCompletedFile)) {
    LOG(INFO) << "CheckSecondStage: First stage not yet completed.";
    return false;
  }
  if (FileExists(kSecondStageCompletedFile)) {
    LOG(INFO) << "CheckSecondStage: Second stage already completed.";
    return false;
  }

  // At this point, we should be in the second stage. We verify, that the other
  // files are in a consistent state.
  if (!FileExists(kUnencryptedStatefulRollbackDataPath)) {
    LOG(ERROR) << "CheckSecondStage: Rollback data "
               << kUnencryptedStatefulRollbackDataPath.value()
               << " should exist in second stage.";
    return false;
  }
  if (!FileExists(kEncryptedStatefulRollbackDataPath)) {
    LOG(ERROR) << "CheckSecondStage: Rollback data "
               << kEncryptedStatefulRollbackDataPath.value()
               << " should exist in second stage.";
    return false;
  }

  LOG(INFO) << "CheckSecondStage: OK.";
  return true;
}

bool OobeConfig::CheckThirdStage() const {
  if (!FileExists(kSecondStageCompletedFile)) {
    LOG(INFO) << "CheckThirdStage: Second stage not yet completed.";
    return false;
  }

  // At this point, we should be in the third stage. We verify, that the other
  // files are in a consistent state.
  if (!FileExists(kFirstStageCompletedFile)) {
    LOG(ERROR) << "CheckThirdStage: First stage should be already completed.";
    return false;
  }
  if (FileExists(kUnencryptedStatefulRollbackDataPath)) {
    LOG(ERROR) << "CheckThirdStage: Rollback data "
               << kUnencryptedStatefulRollbackDataPath.value()
               << " should not exist in third stage.";
    return false;
  }
  if (!FileExists(kEncryptedStatefulRollbackDataPath)) {
    LOG(ERROR) << "CheckThirdStage: Rollback data "
               << kEncryptedStatefulRollbackDataPath.value()
               << " should exist in third stage.";
    return false;
  }

  LOG(INFO) << "CheckThirdStage: OK.";
  return true;
}

bool OobeConfig::ShouldSaveRollbackData() const {
  return FileExists(kRollbackSaveMarkerFile);
}

bool OobeConfig::DeleteRollbackSaveFlagFile() const {
  return base::DeleteFile(GetPrefixedFilePath(kRollbackSaveMarkerFile));
}

}  // namespace oobe_config
