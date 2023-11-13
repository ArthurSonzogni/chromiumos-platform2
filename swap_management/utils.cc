// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/status.h"
#include "swap_management/utils.h"

#include <fcntl.h>
#include <limits>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <sys/mount.h>

namespace swap_management {

namespace {
Utils* util_ = nullptr;
}  // namespace

Utils* Utils::Get() {
  [[maybe_unused]] static bool created = []() -> bool {
    if (!util_)
      util_ = new Utils;
    return true;
  }();

  return util_;
}

void Utils::OverrideForTesting(Utils* util) {
  util_ = util;
}

// Helper function to run binary.
// On success, store stdout in |output| and return absl::OkStatus()
// On failure, return corresponding absl error based on errno and append stderr.
absl::Status Utils::RunProcessHelper(const std::vector<std::string>& commands,
                                     std::string* output) {
  if (commands.empty())
    return absl::InvalidArgumentError("Empty input for RunProcessHelper.");

  brillo::ProcessImpl process;
  for (auto& com : commands)
    process.AddArg(com);

  process.RedirectOutputToMemory(true);

  if (process.Run() != EXIT_SUCCESS)
    return ErrnoToStatus(errno, process.GetOutputString(STDOUT_FILENO));

  *output = process.GetOutputString(STDOUT_FILENO);

  return absl::OkStatus();
}

// Same as the previous one, but log stdout instead of send it back.
absl::Status Utils::RunProcessHelper(const std::vector<std::string>& commands) {
  std::string output;
  absl::Status status = absl::OkStatus();

  status = RunProcessHelper(commands, &output);
  if (!status.ok())
    return status;

  if (!output.empty())
    LOG(INFO) << commands[0] << ": " << output;

  return absl::OkStatus();
}

absl::Status Utils::WriteFile(const base::FilePath& path,
                              const std::string& data) {
  if (!base::WriteFile(path, data))
    return ErrnoToStatus(errno, "Failed to write " + path.value());

  return absl::OkStatus();
}

absl::Status Utils::ReadFileToStringWithMaxSize(const base::FilePath& path,
                                                std::string* contents,
                                                size_t max_size) {
  if (!base::ReadFileToStringWithMaxSize(path, contents, max_size))
    return ErrnoToStatus(errno, "Failed to read " + path.value());

  return absl::OkStatus();
}

absl::Status Utils::ReadFileToString(const base::FilePath& path,
                                     std::string* contents) {
  return ReadFileToStringWithMaxSize(path, contents,
                                     std::numeric_limits<size_t>::max());
}

absl::Status Utils::DeleteFile(const base::FilePath& path) {
  if (!brillo::DeleteFile(path))
    return ErrnoToStatus(errno, "Failed to delete " + path.value());

  return absl::OkStatus();
}

absl::Status Utils::PathExists(const base::FilePath& path) {
  if (!base::PathExists(path))
    return ErrnoToStatus(errno, path.value() + " does not exist.");

  return absl::OkStatus();
}

// Extend file at |path| to the size |size|.
absl::Status Utils::Fallocate(const base::FilePath& path, size_t size) {
  absl::Status status = absl::OkStatus();

  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  if (HANDLE_EINTR(fallocate(file.GetPlatformFile(), 0, 0,
                             static_cast<off_t>(size))) == -1)
    status = ErrnoToStatus(errno, "Can not extend " + path.value() +
                                      " to size " + std::to_string(size));

  file.Close();
  return status;
}

absl::Status Utils::CreateDirectory(const base::FilePath& path) {
  if (!base::CreateDirectory(path))
    return ErrnoToStatus(errno, "Can not create " + path.value());

  return absl::OkStatus();
}

absl::Status Utils::SetPosixFilePermissions(const base::FilePath& path,
                                            int mode) {
  if (!base::SetPosixFilePermissions(path, mode))
    return ErrnoToStatus(errno, "Failed to set permission for " + path.value() +
                                    " to " + std::to_string(mode));

  return absl::OkStatus();
}

absl::Status Utils::Mount(const std::string& source,
                          const std::string& target,
                          const std::string& fs_type,
                          uint64_t mount_flags,
                          const std::string& data) {
  if (mount(source.c_str(), target.c_str(), fs_type.c_str(), mount_flags,
            data.c_str()) == -1)
    return ErrnoToStatus(errno, "Failed to mount " + target);

  return absl::OkStatus();
}

absl::Status Utils::Umount(const std::string& target) {
  if (umount(target.c_str()) == -1)
    return ErrnoToStatus(errno, "Failed to umount " + target);

  return absl::OkStatus();
}

absl::StatusOr<struct statfs> Utils::GetStatfs(const std::string& path) {
  struct statfs sf = {};

  if (statfs(path.c_str(), &sf) == -1)
    return ErrnoToStatus(errno, "Failed to read statfs for " + path);

  return std::move(sf);
}

absl::StatusOr<std::string> Utils::GenerateRandHex(size_t size) {
  std::string random_bytes = base::RandBytesAsString(size);
  if (random_bytes.size() != size)
    return ErrnoToStatus(errno, " Failed to generate random hex with size" +
                                    std::to_string(size));

  return base::HexEncode(random_bytes.data(), random_bytes.size());
}

absl::StatusOr<base::SystemMemoryInfoKB> Utils::GetSystemMemoryInfo() {
  base::SystemMemoryInfoKB meminfo;
  if (!base::GetSystemMemoryInfo(&meminfo))
    return absl::NotFoundError("Could not get MemTotal in /proc/meminfo");

  return std::move(meminfo);
}

const base::FilePath ScopedFilePathTraits::InvalidValue() {
  return base::FilePath();
}

void ScopedFilePathTraits::Free(const base::FilePath path) {
  absl::Status status = Utils::Get()->DeleteFile(path);
  LOG_IF(ERROR, !status.ok()) << status;
}

}  // namespace swap_management
