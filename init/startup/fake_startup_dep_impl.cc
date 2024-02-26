// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/types.h>

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>

#include "init/startup/fake_startup_dep_impl.h"

namespace startup {

FakeStartupDep::FakeStartupDep() : StartupDep() {}

void FakeStartupDep::SetStatResultForPath(const base::FilePath& path,
                                          const struct stat& st) {
  result_map_[path.value()] = st;
}

void FakeStartupDep::SetStatvfsResultForPath(const base::FilePath& path,
                                             const struct statvfs& st) {
  result_statvfs_map_[path.value()] = st;
}

void FakeStartupDep::SetMountResultForPath(const base::FilePath& path,
                                           const std::string& output) {
  mount_result_map_[path.value()] = output;
}

void FakeStartupDep::SetIoctlReturnValue(int ret) {
  ioctl_ret_ = ret;
}

void FakeStartupDep::SetMountEncOutputForArg(const std::string& arg,
                                             const std::string& output) {
  mount_enc_result_map_[arg] = output;
}

int FakeStartupDep::GetBootAlertForArg(const std::string& arg) {
  return alert_result_map_[arg];
}

void FakeStartupDep::SetClobberLogFile(const base::FilePath& path) {
  clobber_log_ = path;
}

std::set<std::string> FakeStartupDep::GetClobberArgs() {
  return clobber_args_;
}

bool FakeStartupDep::Stat(const base::FilePath& path, struct stat* st) {
  std::unordered_map<std::string, struct stat>::iterator it;
  it = result_map_.find(path.value());
  if (st == nullptr || it == result_map_.end()) {
    return false;
  }

  *st = it->second;
  return true;
}

bool FakeStartupDep::Statvfs(const base::FilePath& path, struct statvfs* st) {
  std::unordered_map<std::string, struct statvfs>::iterator it;
  it = result_statvfs_map_.find(path.value());
  if (st == nullptr || it == result_statvfs_map_.end()) {
    return false;
  }

  *st = it->second;
  return true;
}

bool FakeStartupDep::Mount(const base::FilePath& src,
                           const base::FilePath& dst,
                           const std::string& type,
                           unsigned long flags,  // NOLINT(runtime/int)
                           const std::string& data) {
  std::unordered_map<std::string, std::string>::iterator it;
  it = mount_result_map_.find(dst.value());

  if (it == mount_result_map_.end()) {
    return false;
  }

  return src.value().compare(it->second) == 0;
}

bool FakeStartupDep::Umount(const base::FilePath& path) {
  umount_vector_.push_back(path.value());
  return true;
}

base::ScopedFD FakeStartupDep::Open(const base::FilePath& pathname, int flags) {
  return base::ScopedFD(open_ret_);
}

// NOLINTNEXTLINE(runtime/int)
int FakeStartupDep::Ioctl(int fd, unsigned long request, int* arg1) {
  return ioctl_ret_;
}

int FakeStartupDep::MountEncrypted(const std::vector<std::string>& args,
                                   std::string* output) {
  std::string arg;
  if (!args.empty()) {
    arg = args.at(0);
  }
  if (mount_enc_result_map_.count(arg) == 0) {
    return -1;
  }

  *output = mount_enc_result_map_[arg];
  return 0;
}

void FakeStartupDep::BootAlert(const std::string& arg) {
  alert_result_map_[arg] = 1;
}

void FakeStartupDep::RemoveInBackground(
    const std::vector<base::FilePath>& paths) {
  for (auto path : paths) {
    brillo::DeletePathRecursively(path);
  }
}

void FakeStartupDep::ClobberLog(const std::string& msg) {
  WriteFile(clobber_log_, msg);
}

void FakeStartupDep::Clobber(const std::string& boot_alert_msg,
                             const std::vector<std::string>& args,
                             const std::string& clobber_log_msg) {
  BootAlert(boot_alert_msg);
  ClobberLog(clobber_log_msg);
  for (std::string arg : args) {
    clobber_args_.insert(arg);
  }
}

}  // namespace startup
