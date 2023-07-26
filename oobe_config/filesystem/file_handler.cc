// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/filesystem/file_handler.h"

#include <optional>
#include <vector>

#include <base/posix/eintr_wrapper.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <sys/file.h>

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

bool FileHandler::HasOpensslEncryptedRollbackData() const {
  return base::PathExists(
      GetFullPath(kPreservePath).Append(kOpensslEncryptedRollbackDataFileName));
}

bool FileHandler::ReadOpensslEncryptedRollbackData(
    std::string* openssl_encrypted_rollback_data) const {
  return base::ReadFileToString(
      GetFullPath(kPreservePath).Append(kOpensslEncryptedRollbackDataFileName),
      openssl_encrypted_rollback_data);
}

bool FileHandler::WriteOpensslEncryptedRollbackData(
    const std::string& openssl_encrypted_rollback_data) const {
  return base::WriteFile(
      GetFullPath(kPreservePath).Append(kOpensslEncryptedRollbackDataFileName),
      openssl_encrypted_rollback_data);
}

bool FileHandler::RemoveOpensslEncryptedRollbackData() const {
  return base::DeleteFile(
      GetFullPath(kPreservePath).Append(kOpensslEncryptedRollbackDataFileName));
}

bool FileHandler::HasTpmEncryptedRollbackData() const {
  return base::PathExists(
      GetFullPath(kPreservePath).Append(kTpmEncryptedRollbackDataFileName));
}

bool FileHandler::ReadTpmEncryptedRollbackData(
    std::string* tpm_encrypted_rollback_data) const {
  return base::ReadFileToString(
      GetFullPath(kPreservePath).Append(kTpmEncryptedRollbackDataFileName),
      tpm_encrypted_rollback_data);
}

bool FileHandler::WriteTpmEncryptedRollbackData(
    const std::string& tpm_encrypted_rollback_data) const {
  return base::WriteFile(
      GetFullPath(kPreservePath).Append(kTpmEncryptedRollbackDataFileName),
      tpm_encrypted_rollback_data);
}

bool FileHandler::RemoveTpmEncryptedRollbackData() const {
  return base::DeleteFile(
      GetFullPath(kPreservePath).Append(kTpmEncryptedRollbackDataFileName));
}

bool FileHandler::HasDecryptedRollbackData() const {
  return base::PathExists(
      GetFullPath(kDataRestorePath).Append(kDecryptedRollbackDataFileName));
}

bool FileHandler::ReadDecryptedRollbackData(
    std::string* decrypted_rollback_data) const {
  return base::ReadFileToString(
      GetFullPath(kDataRestorePath).Append(kDecryptedRollbackDataFileName),
      decrypted_rollback_data);
}

bool FileHandler::WriteDecryptedRollbackData(
    const std::string& decrypted_rollback_data) const {
  return base::WriteFile(
      GetFullPath(kDataRestorePath).Append(kDecryptedRollbackDataFileName),
      decrypted_rollback_data);
}

bool FileHandler::RemoveDecryptedRollbackData() const {
  return base::DeleteFile(
      GetFullPath(kDataRestorePath).Append(kDecryptedRollbackDataFileName));
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

bool FileHandler::HasRollbackMetricsData() const {
  return base::PathExists(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName));
}

bool FileHandler::CreateRollbackMetricsDataAtomically(
    const std::string& rollback_metrics_metadata) const {
  if (!base::ImportantFileWriter::WriteFileAtomically(
          GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName),
          rollback_metrics_metadata)) {
    LOG(ERROR)
        << "Failed to create and write Rollback metrics file atomically.";
    return false;
  }

  return true;
}

std::optional<base::File> FileHandler::OpenRollbackMetricsDataFile() const {
  return OpenFile(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName));
}

bool FileHandler::ReadRollbackMetricsData(
    std::string* rollback_metrics_data) const {
  return base::ReadFileToString(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName),
      rollback_metrics_data);
}

bool FileHandler::RemoveRollbackMetricsData() const {
  return base::DeleteFile(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName));
}

std::optional<base::Time> FileHandler::LastModifiedTimeRollbackMetricsDataFile()
    const {
  base::File::Info file_info;
  if (!base::GetFileInfo(
          GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName),
          &file_info)) {
    return std::nullopt;
  }

  return file_info.last_modified;
}

base::FileEnumerator FileHandler::RamoopsFileEnumerator() const {
  return base::FileEnumerator(GetFullPath(kRamoopsPath),
                              /*recursive=*/false, base::FileEnumerator::FILES,
                              kRamoopsFilePattern);
}

std::optional<base::File> FileHandler::OpenFile(
    const base::FilePath& path) const {
  base::File file;
  file.Initialize(path, base::File::FLAG_READ | base::File::FLAG_OPEN |
                            base::File::FLAG_APPEND);
  if (!file.IsValid()) {
    return std::nullopt;
  }
  return file;
}

bool FileHandler::LockFileNoBlocking(const base::File& file) const {
  // base::File locking uses POSIX record locks instead of flock. We get the
  // file descriptor and make the system call to flock manually.
  if (HANDLE_EINTR(flock(file.GetPlatformFile(), LOCK_EX | LOCK_NB)) < 0) {
    return false;
  }
  return true;
}

std::optional<std::string> FileHandler::GetOpenedFileData(
    base::File& file) const {
  // Read the full content of the file from the beginning.
  std::vector<char> file_content(file.GetLength());
  if (file.Read(0, file_content.data(), file_content.size()) !=
      file_content.size()) {
    LOG(ERROR) << "Unexpected data file read length.";
    return std::nullopt;
  }

  std::string data(file_content.begin(), file_content.end());
  return data;
}

bool FileHandler::ExtendOpenedFile(base::File& file,
                                   const std::string& data) const {
  // File is opened in append mode; we can write the event data to the current
  // position to extend it.
  int initial_length = file.GetLength();
  if (file.WriteAtCurrentPos(data.c_str(), data.length()) != data.length()) {
    LOG(ERROR) << "Unable to write data in file.";
    return false;
  }

  if (file.GetLength() != (initial_length + data.length())) {
    // If the lengths do not match, the output file is not the expected one.
    LOG(ERROR) << "The file is corrupted.";
    return false;
  }

  return true;
}

void FileHandler::TruncateOpenedFile(base::File& file, const int length) const {
  file.SetLength(length);
}

void FileHandler::UnlockFile(base::File& file) const {
  std::ignore = flock(file.GetPlatformFile(), LOCK_UN);
  return;
}

base::FilePath FileHandler::GetFullPath(
    const std::string& path_without_root) const {
  return root_.Append(path_without_root);
}

}  // namespace oobe_config
