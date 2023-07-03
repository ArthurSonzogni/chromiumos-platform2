// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/filesystem/file_handler_for_testing.h"

#include <signal.h>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <brillo/streams/file_stream.h>

#include "oobe_config/filesystem/file_handler.h"

namespace oobe_config {

FileHandlerForTesting::FileHandlerForTesting() {
  CHECK(fake_root_dir_.CreateUniqueTempDir());
  root_ = fake_root_dir_.GetPath();
}

FileHandlerForTesting::FileHandlerForTesting(FileHandlerForTesting&&) noexcept =
    default;
FileHandlerForTesting& FileHandlerForTesting::operator=(
    FileHandlerForTesting&&) noexcept = default;

FileHandlerForTesting::~FileHandlerForTesting() = default;

bool FileHandlerForTesting::CreateDefaultExistingPaths() const {
  return CreateRamoopsPath() && CreateSavePath() && CreatePreservePath() &&
         CreateRestorePath() && CreateChronosPath();
}

bool FileHandlerForTesting::CreateRestorePath() const {
  return base::CreateDirectory(GetFullPath(kDataRestorePath));
}

bool FileHandlerForTesting::CreateSavePath() const {
  return base::CreateDirectory(GetFullPath(kDataSavePath));
}

bool FileHandlerForTesting::CreatePreservePath() const {
  return base::CreateDirectory(GetFullPath(kPreservePath));
}

bool FileHandlerForTesting::CreateRamoopsPath() const {
  return base::CreateDirectory(GetFullPath(kRamoopsPath));
}

bool FileHandlerForTesting::CreateChronosPath() const {
  return base::CreateDirectory(GetFullPath(kChronosPath));
}

bool FileHandlerForTesting::HasDataSavedFlag() const {
  return base::PathExists(
      GetFullPath(kDataSavePath).Append(kDataSavedFileName));
}

bool FileHandlerForTesting::CreateOobeCompletedFlag() const {
  return base::WriteFile(
      GetFullPath(kChronosPath).Append(kOobeCompletedFileName), "");
}

bool FileHandlerForTesting::CreateMetricsReportingEnabledFile() const {
  return base::WriteFile(
      GetFullPath(kChronosPath).Append(kMetricsReportingEnabledFileName), "");
}

bool FileHandlerForTesting::RemoveMetricsReportingEnabledFile() const {
  return base::DeleteFile(
      GetFullPath(kChronosPath).Append(kMetricsReportingEnabledFileName));
}

bool FileHandlerForTesting::ReadRollbackMetricsData(
    std::string* rollback_metrics_data) const {
  return base::ReadFileToString(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName),
      rollback_metrics_data);
}

bool FileHandlerForTesting::WriteRollbackMetricsData(
    const std::string& data) const {
  return base::WriteFile(
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName), data);
}

bool FileHandlerForTesting::ReadPstoreData(std::string* data) const {
  return base::ReadFileToString(
      GetFullPath(kDataSavePath).Append(kPstoreFileName), data);
}

bool FileHandlerForTesting::WriteRamoopsData(const std::string& data) const {
  return base::WriteFile(GetFullPath(kRamoopsPath).Append(kRamoops0FileName),
                         data);
}

bool FileHandlerForTesting::RemoveRamoopsData() const {
  return base::DeleteFile(GetFullPath(kRamoopsPath).Append(kRamoops0FileName));
}

std::unique_ptr<brillo::Process>
FileHandlerForTesting::StartLockMetricsFileProcess(
    const base::FilePath& build_directory) const {
  CHECK(!build_directory.empty());
  auto lock_process = std::make_unique<brillo::ProcessImpl>();
  base::FilePath metrics_file =
      GetFullPath(kPreservePath).Append(kRollbackMetricsDataFileName);
  base::FilePath lock_file_holder =
      build_directory.Append("hold_lock_file_for_tests");
  lock_process->AddArg(lock_file_holder.value());
  lock_process->AddArg(metrics_file.value());
  lock_process->RedirectUsingPipe(STDOUT_FILENO, false);
  CHECK(lock_process->Start());

  pid_t pid = lock_process->pid();
  LOG(INFO) << "Started lock process with pid " << pid << ".";
  const int kill_timeout = 5;

  // Wait for process to lock file. It writes in the stream when done.
  brillo::StreamPtr stdout = brillo::FileStream::FromFileDescriptor(
      lock_process->GetPipe(STDOUT_FILENO), /*own_descriptor=*/false, nullptr);
  if (!stdout) {
    lock_process->Kill(SIGKILL, kill_timeout);
    return nullptr;
  }

  std::string locked_msg = "file_is_locked";
  std::vector<char> buf(locked_msg.size());
  if (!stdout->ReadAllBlocking(buf.data(), buf.size(), /*error=*/nullptr)) {
    lock_process->Kill(SIGKILL, kill_timeout);
    return nullptr;
  }

  return lock_process;
}

base::FilePath FileHandlerForTesting::GetFullPath(
    const std::string& path_without_root) const {
  return FileHandler::GetFullPath(path_without_root);
}

}  // namespace oobe_config
