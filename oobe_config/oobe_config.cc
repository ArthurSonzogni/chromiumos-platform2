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

#include "oobe_config/pstore_storage.h"
#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_data.pb.h"
#include "oobe_config/rollback_openssl_encryption.h"

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

  // Encrypt data with software and store the key in pstore.
  // TODO(crbug/1212958) add TPM based encryption.

  base::Optional<EncryptedData> encrypted_rollback_data =
      Encrypt(brillo::SecureBlob(serialized_rollback_data));

  if (!encrypted_rollback_data) {
    LOG(ERROR) << "Failed to encrypt, not saving any rollback data.";
    return false;
  }

  if (!StageForPstore(encrypted_rollback_data->key.to_string(),
                      prefix_path_for_testing_)) {
    LOG(ERROR)
        << "Failed to prepare data for storage in the encrypted reboot vault";
    return false;
  }

  if (!WriteFile(kUnencryptedStatefulRollbackDataPath,
                 brillo::BlobToString(encrypted_rollback_data->data))) {
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
  LOG(INFO) << "Fetching key from pstore.";
  base::Optional<std::string> key = LoadFromPstore(prefix_path_for_testing_);
  if (!key.has_value()) {
    LOG(ERROR) << "Failed to load key from pstore.";
    return false;
  }

  std::string encrypted_data;
  if (!ReadFile(kUnencryptedStatefulRollbackDataPath, &encrypted_data)) {
    return false;
  }
  base::Optional<brillo::SecureBlob> decrypted_data = Decrypt(
      {brillo::BlobFromString(encrypted_data), brillo::SecureBlob(*key)});
  if (!decrypted_data.has_value()) {
    LOG(ERROR) << "Could not decrypt rollback data.";
    return false;
  }

  std::string rollback_data_str = decrypted_data->to_string();

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

bool OobeConfig::ShouldRestoreRollbackData() const {
  return FileExists(kUnencryptedStatefulRollbackDataPath);
}

bool OobeConfig::ShouldSaveRollbackData() const {
  return FileExists(kRollbackSaveMarkerFile);
}

bool OobeConfig::DeleteRollbackSaveFlagFile() const {
  return base::DeleteFile(GetPrefixedFilePath(kRollbackSaveMarkerFile));
}

}  // namespace oobe_config
