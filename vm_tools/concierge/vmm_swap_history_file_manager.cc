// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_history_file_manager.h"

#include <fcntl.h>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>

namespace vm_tools::concierge {

VmmSwapHistoryFileManager::VmmSwapHistoryFileManager(base::FilePath path)
    : path_(path) {}

base::File VmmSwapHistoryFileManager::Create() const {
  base::File file = base::File(
      open(path_.value().c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600));
  if (!file.IsValid()) {
    file = base::File(base::File::GetLastFileError());
  }
  return file;
}

base::File VmmSwapHistoryFileManager::CreateRotationFile() const {
  base::File file =
      base::File(open(RotationFilePath().value().c_str(),
                      O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600));
  if (!file.IsValid()) {
    file = base::File(base::File::GetLastFileError());
  }
  return file;
}

base::File VmmSwapHistoryFileManager::Open() const {
  base::File file = base::File(open(path_.value().c_str(), O_RDWR | O_CLOEXEC));
  if (!file.IsValid()) {
    file = base::File(base::File::GetLastFileError());
  }
  return file;
}

base::File VmmSwapHistoryFileManager::OpenFileInDir(
    std::string file_name) const {
  base::File file = base::File(open(
      path_.DirName().Append(file_name).value().c_str(), O_RDWR | O_CLOEXEC));
  if (!file.IsValid()) {
    file = base::File(base::File::GetLastFileError());
  }
  return file;
}

bool VmmSwapHistoryFileManager::Rotate() const {
  base::File::Error error;
  bool result = base::ReplaceFile(RotationFilePath(), path_, &error);
  if (!result) {
    LOG(ERROR) << "Failed to replace history file: "
               << base::File::ErrorToString(error);
  }
  return result;
}

void VmmSwapHistoryFileManager::Delete() const {
  if (!brillo::DeleteFile(path_)) {
    LOG(ERROR) << "Failed to delete history file.";
  }
}

void VmmSwapHistoryFileManager::DeleteRotationFile() const {
  if (!brillo::DeleteFile(RotationFilePath())) {
    LOG(ERROR) << "Failed to delete rotation history file.";
  }
}

void VmmSwapHistoryFileManager::DeleteFileInDir(std::string file_name) const {
  base::FilePath path = path_.DirName().Append(file_name);
  if (!brillo::DeleteFile(path)) {
    LOG(ERROR) << "Failed to delete file: " << path;
  }
}

base::FilePath VmmSwapHistoryFileManager::RotationFilePath() const {
  return path_.AddExtension("tmp");
}

}  // namespace vm_tools::concierge
