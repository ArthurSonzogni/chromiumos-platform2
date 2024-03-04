// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <init/process_killer/process_killer.h>

#include <sys/signal.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <init/process_killer/process.h>
#include <init/process_killer/process_manager.h>

namespace init {
namespace {
constexpr char const* kSessionMountRegexes[] = {
    "/data",
    "/home/.shadow/[a-fA-F0-9]{40}/mount",
    "/home/chronos/u-[a-fA-F0-9]{40}",
    "/home/chronos/user",
    "/home/root/[a-fA-F0-9]{40}",
    "/home/user/[a-fA-F0-9]{40}",
    "/run/daemon-store",
};

constexpr char const* kSystemMountRegexes[] = {"/var", "/home", "/usr/local",
                                               "/mnt/stateful_partition"};

constexpr char const* kSessionDeviceRegexes[] = {
    "/dev/mapper/dmcrypt-",
};

constexpr char const* kSystemDeviceRegexes[] = {
    "/dev/mapper/encstateful",
    "/dev/sd[a-z]1",
    "/dev/mmcblk[0-9]p1",
    "/dev/nvme[0-9]n[0-9]p1",
};

constexpr char kMatchNothingRegex[] = "$^";

constexpr int kKillerIterations = 10;
constexpr base::TimeDelta kSleepInterval = base::Milliseconds(100);

re2::RE2 ConstructMountRegex(bool session,
                             bool shutdown,
                             const std::string& mount_filter) {
  std::vector<std::string> mounts;
  if (session) {
    for (auto mount : kSessionMountRegexes) {
      mounts.push_back(mount);
    }
  }

  if (shutdown) {
    for (auto mount : kSystemMountRegexes) {
      mounts.push_back(mount);
    }
  }

  if (mount_filter.size()) {
    auto filter = base::JSONReader::ReadAndReturnValueWithError(mount_filter);
    if (!filter.has_value()) {
      LOG(ERROR) << "Could not parse the mount filter JSON list. Error: "
                 << filter.error().message;
    } else if (!filter->is_list()) {
      LOG(ERROR) << "Invalid mount filter JSON, expecting path regex list.";
    } else {
      for (const auto& mount : filter->GetList()) {
        if (auto* mount_str = mount.GetIfString();
            mount_str && mount_str->size()) {
          mounts.push_back(*mount_str);
        }
      }
    }
  }

  if (mounts.empty()) {
    // Avoid empty regex which matches to any string, return
    // `kMatchNothingRegex` instead.
    return kMatchNothingRegex;
  }
  return re2::RE2("^(" + base::JoinString(mounts, "|") + ")");
}

re2::RE2 ConstructDeviceRegex(bool session, bool shutdown) {
  std::vector<std::string> devices;
  if (session) {
    for (auto device : kSessionDeviceRegexes) {
      devices.push_back(device);
    }
  }

  if (shutdown) {
    for (auto device : kSystemDeviceRegexes) {
      devices.push_back(device);
    }
  }

  if (devices.empty()) {
    return kMatchNothingRegex;
  }
  return re2::RE2("(" + base::JoinString(devices, "|") + ")");
}

}  // namespace

ProcessKiller::ProcessKiller(bool session,
                             bool shutdown,
                             const std::string& mount_filter)
    : mount_regex_(ConstructMountRegex(session, shutdown, mount_filter)),
      device_regex_(ConstructDeviceRegex(session, shutdown)),
      pm_(std::make_unique<ProcessManager>(base::FilePath("/proc"))) {}

void ProcessKiller::LogProcesses() {
  for (ActiveProcess& p : process_list_) {
    p.LogProcess(mount_regex_, device_regex_);
  }
}

void ProcessKiller::KillProcesses(bool files, bool devices) {
  // First try sending SIGTERM.
  for (int i = 0; i < kKillerIterations; i++) {
    UpdateProcessList(files, devices);
    if (process_list_.empty())
      return;
    LOG(INFO) << "Sending SIGTERM";
    LogProcesses();
    for (ActiveProcess& p : process_list_) {
      pm_->SendSignalToProcess(p, SIGTERM);
    }
    base::PlatformThread::Sleep(kSleepInterval);
  }

  // If processes are still running, send SIGKILL.
  for (int i = 0; i < kKillerIterations; i++) {
    UpdateProcessList(files, devices);
    if (process_list_.empty())
      return;
    LOG(INFO) << "Sending SIGKILL";
    LogProcesses();
    for (ActiveProcess& p : process_list_) {
      pm_->SendSignalToProcess(p, SIGKILL);
    }
    base::PlatformThread::Sleep(kSleepInterval);
  }

  // Check processes still active and log.
  UpdateProcessList(files, devices);

  if (!process_list_.empty()) {
    LOG(INFO) << "Processes still active:";
    LogProcesses();
  }
}

void ProcessKiller::UpdateProcessList(bool files, bool devices) {
  process_list_ = pm_->GetProcessList(files, devices);

  process_list_.erase(
      std::remove_if(process_list_.begin(), process_list_.end(),
                     [this, files, devices](const ActiveProcess& p) {
                       bool dont_kill_this_process = true;
                       // Kill processes with a file open that matches the mount
                       // regex.
                       if (files && p.HasFileOpenOnMount(mount_regex_))
                         dont_kill_this_process = false;
                       // Kill processes with a non-init mount namespace and a
                       // mount open that matches the device regex.
                       if (devices && !p.InInitMountNamespace() &&
                           p.HasMountOpenFromDevice(device_regex_))
                         dont_kill_this_process = false;

                       if (!dont_kill_this_process && p.GetPid() == 1) {
                         LOG(ERROR) << "Cowardly refusing to kill init";
                         return true;
                       }
                       return dont_kill_this_process;
                     }),
      process_list_.end());
}

}  // namespace init
