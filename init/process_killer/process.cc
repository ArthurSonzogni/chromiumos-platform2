// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <init/process_killer/process.h>

#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <re2/re2.h>

namespace init {

ActiveProcess::ActiveProcess(
    pid_t pid,
    const std::string& comm,
    const std::vector<ActiveMount>& mounts,
    const std::vector<OpenFileDescriptor>& file_descriptors)
    : pid_(pid),
      comm_(comm),
      mounts_(mounts),
      file_descriptors_(file_descriptors) {}

bool ActiveProcess::HasFileOpenOnMount(const re2::RE2& pattern) const {
  int open_fds =
      std::count_if(file_descriptors_.begin(), file_descriptors_.end(),
                    [&pattern](const OpenFileDescriptor& fd) {
                      return re2::RE2::PartialMatch(fd.path.value(), pattern);
                    });

  return open_fds > 0;
}

bool ActiveProcess::HasMountOpenFromDevice(const re2::RE2& pattern) const {
  int open_mounts = std::count_if(
      mounts_.begin(), mounts_.end(), [&pattern](const ActiveMount& mount) {
        return re2::RE2::PartialMatch(mount.device, pattern);
      });

  return open_mounts > 0;
}

void ActiveProcess::LogProcess() const {
  LOG(INFO) << "Process: " << pid_ << "; Comm: " << comm_;
  LOG(INFO) << "Process Mounts: (Source, Target, Device)";
  for (auto& m : mounts_)
    LOG(INFO) << ">> " << m.source << " " << m.target << " " << m.device;
  LOG(INFO) << "Open files: (Path)";
  for (auto& fd : file_descriptors_)
    LOG(INFO) << ">> " << fd.path;
}

}  // namespace init
