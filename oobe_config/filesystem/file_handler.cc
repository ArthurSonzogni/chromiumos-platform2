// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/filesystem/file_handler.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>

namespace oobe_config {

FileHandler::FileHandler(const std::string& root_directory)
    : root_(root_directory) {}
FileHandler::FileHandler(const FileHandler&) = default;
FileHandler::FileHandler(FileHandler&&) noexcept = default;
FileHandler& FileHandler::operator=(const FileHandler&) = default;
FileHandler& FileHandler::operator=(FileHandler&&) noexcept = default;

FileHandler::~FileHandler() = default;

bool FileHandler::HasRestorePath() const {
  return base::PathExists(GetFullPath(kDataRestorePath));
}

bool FileHandler::RemoveRestorePath() const {
  return base::DeletePathRecursively(GetFullPath(kDataRestorePath));
}

bool FileHandler::HasEncryptedRollbackData() const {
  return base::PathExists(
      GetFullPath(kPreservePath).Append(kRollbackDataFileName));
}

bool FileHandler::ReadEncryptedRollbackData(
    std::string* encrypted_rollback_data) const {
  base::FilePath encrypted_data_file =
      GetFullPath(kPreservePath).Append(kRollbackDataFileName);

  return base::ReadFileToString(encrypted_data_file, encrypted_rollback_data);
}

bool FileHandler::WriteEncryptedRollbackData(
    const std::string& encrypted_rollback_data) const {
  base::FilePath encrypted_data_file =
      GetFullPath(kPreservePath).Append(kRollbackDataFileName);

  return base::WriteFile(encrypted_data_file, encrypted_rollback_data);
}

bool FileHandler::RemoveEncryptedRollbackData() const {
  return base::DeleteFile(
      GetFullPath(kPreservePath).Append(kRollbackDataFileName));
}

bool FileHandler::HasDecryptedRollbackData() const {
  return base::PathExists(
      GetFullPath(kDataRestorePath).Append(kRollbackDataFileName));
}

bool FileHandler::ReadDecryptedRollbackData(
    std::string* decrypted_rollback_data) const {
  return base::ReadFileToString(
      GetFullPath(kDataRestorePath).Append(kRollbackDataFileName),
      decrypted_rollback_data);
}

bool FileHandler::WriteDecryptedRollbackData(
    const std::string& decrypted_rollback_data) const {
  return base::WriteFile(
      GetFullPath(kDataRestorePath).Append(kRollbackDataFileName),
      decrypted_rollback_data);
}

bool FileHandler::RemoveDecryptedRollbackData() const {
  return base::DeleteFile(
      GetFullPath(kDataRestorePath).Append(kRollbackDataFileName));
}

bool FileHandler::HasRollbackSaveTriggerFlag() const {
  return base::PathExists(GetFullPath(kSaveRollbackDataFile));
}

bool FileHandler::RemoveRollbackSaveTriggerFlag() const {
  return base::DeleteFile(GetFullPath(kSaveRollbackDataFile));
}

bool FileHandler::CreateDataSavedFlag() const {
  return base::WriteFile(GetFullPath(kDataSavePath).Append(kDataSavedFileName),
                         std::string());
}

bool FileHandler::HasOobeCompletedFlag() const {
  return base::PathExists(
      GetFullPath(kChronosPath).Append(kOobeCompletedFileName));
}

bool FileHandler::HasMetricsReportingEnabledFlag() const {
  return base::PathExists(
      GetFullPath(kChronosPath).Append(kMetricsReportingEnabledFileName));
}

bool FileHandler::WritePstoreData(const std::string& data) const {
  return base::WriteFile(GetFullPath(kDataSavePath).Append(kPstoreFileName),
                         data);
}

base::FileEnumerator FileHandler::RamoopsFileEnumerator() const {
  return base::FileEnumerator(GetFullPath(kRamoopsPath),
                              /*recursive=*/false, base::FileEnumerator::FILES,
                              kRamoopsFilePattern);
}

base::FilePath FileHandler::GetFullPath(
    const std::string& path_without_root) const {
  return root_.Append(path_without_root);
}

}  // namespace oobe_config
