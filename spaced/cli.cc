// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// spaced_cli provides a command line interface disk usage queries.

#include <iostream>

#include <base/task/single_thread_task_executor.h>
#include <base/files/file_path.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/run_loop.h>
#include <base/threading/thread.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "spaced/disk_usage_proxy.h"

namespace {
std::string UpdateStateToString(const spaced::StatefulDiskSpaceState& state) {
  switch (state) {
    case spaced::StatefulDiskSpaceState::NONE:
      return "None";
    case spaced::StatefulDiskSpaceState::NORMAL:
      return "Normal";
    case spaced::StatefulDiskSpaceState::LOW:
      return "Low";
    case spaced::StatefulDiskSpaceState::CRITICAL:
      return "Critical";
    default:
      return "Invalid state";
  }
}

// Simply echoes the update received by spaced.
class EchoSpacedObserver : public spaced::SpacedObserverInterface {
 public:
  EchoSpacedObserver() = default;
  ~EchoSpacedObserver() override = default;

  void OnStatefulDiskSpaceUpdate(
      const spaced::StatefulDiskSpaceUpdate& update) override {
    std::cout << "State: " << UpdateStateToString(update.state())
              << ", Available space (bytes): " << update.free_space_bytes()
              << std::endl;
    fflush(stdout);
  }
};

}  // namespace

int main(int argc, char** argv) {
  DEFINE_string(get_free_disk_space, "",
                "Gets free disk space available on the given path");
  DEFINE_string(get_total_disk_space, "",
                "Gets total disk space available on the given path");
  DEFINE_bool(get_root_device_size, false, "Gets the size of the root device");
  DEFINE_bool(monitor_stateful, false,
              "Monitors the space available on the stateful partition");

  brillo::FlagHelper::Init(argc, argv, "Chromium OS Space Daemon CLI");

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher{task_executor.task_runner()};

  std::unique_ptr<spaced::DiskUsageProxy> disk_usage_proxy =
      spaced::DiskUsageProxy::Generate();

  if (!disk_usage_proxy) {
    LOG(ERROR) << "Failed to get disk usage proxy";
    return 1;
  }

  if (!FLAGS_get_free_disk_space.empty()) {
    std::cout << disk_usage_proxy->GetFreeDiskSpace(
        base::FilePath(FLAGS_get_free_disk_space));
    return 0;
  } else if (!FLAGS_get_total_disk_space.empty()) {
    std::cout << disk_usage_proxy->GetTotalDiskSpace(
        base::FilePath(FLAGS_get_total_disk_space));
    return 0;
  } else if (FLAGS_get_root_device_size) {
    std::cout << disk_usage_proxy->GetRootDeviceSize();
    return 0;
  } else if (FLAGS_monitor_stateful) {
    EchoSpacedObserver observer;
    disk_usage_proxy->AddObserver(&observer);
    disk_usage_proxy->StartMonitoring();
    // Infinite loop; let the user interrupt monitoring with Ctrl+C.
    base::RunLoop().Run();
    return 0;
  }

  return 1;
}
