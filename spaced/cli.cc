// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// spaced_cli provides a command line interface disk usage queries.

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <locale>
#include <string>

#include <base/command_line.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_executor.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "spaced/disk_usage_proxy.h"

namespace {

bool human_readable_sizes = false;

enum Size : int64_t;

std::ostream& operator<<(std::ostream& out, const Size size) {
  const int64_t i = static_cast<int64_t>(size);

  if (i < 0) {
    return out << "error (" << i << ")";
  }

  out << i;

  if (!human_readable_sizes) {
    return out;
  }

  out << " bytes";

  if (i < 1024) {
    return out;
  }

  double d = static_cast<double>(i) / 1024;
  const char* unit = "KMGT";
  while (d >= 1024 && *unit != '\0') {
    d /= 1024;
    unit++;
  }

  const int precision = d < 10 ? 2 : d < 100 ? 1 : 0;
  return out << base::StringPrintf(" (%.*f %c)", precision, d, *unit);
}

class NumPunct : public std::numpunct<char> {
 private:
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

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
    std::cout << "Time: " << base::Time::Now()
              << ", State: " << UpdateStateToString(update.state())
              << ", Available space: " << Size(update.free_space_bytes())
              << std::endl;
  }
};

}  // namespace

int main(int argc, char** argv) {
  DEFINE_string(get_free_disk_space, "",
                "Gets free disk space available on the given path");
  DEFINE_string(get_total_disk_space, "",
                "Gets total disk space available on the given path");
  DEFINE_bool(get_root_device_size, false, "Gets the size of the root device");
  DEFINE_bool(monitor, false,
              "Monitors the space available on the stateful partition,"
              " updates if the delta is greater than 100MB or the total"
              " disk space is less than 1GB");
  DEFINE_bool(human, false, "Print human-readable numbers");
  DEFINE_bool(get_quota_usage, false,
              "Get quota usage information for the given path,"
              " use --uid, --gid, or --project_id to specify specific ids");
  DEFINE_string(uid, "",
                "Use with --get_quota_usage,"
                " gets the quota usage information for the given UID");
  DEFINE_string(gid, "",
                "Use with --get_quota_usage,"
                " gets the quota usage information for the given GID");
  DEFINE_string(project_id, "",
                "Use with --get_quota_usage,"
                " gets the quota usage information for the given project ID");

  DEFINE_string(get_disk_io_stats_for_paths, "",
                "Use with --get_disk_io_stats_for_paths, "
                " gets the disk I/O stats for the specified comma-separated "
                " list of paths");
  brillo::FlagHelper::Init(argc, argv,
                           "ChromiumOS Space Daemon CLI\n\n"
                           "Usage: spaced_cli [options] [path]\n");

  std::string nl;
  if (FLAGS_human) {
    // Ensure that outputted numbers have thousands separators. It makes big
    // numbers much easier to read for a human (eg sizes expressed in bytes).
    std::cout.imbue(std::locale(std::locale::classic(), new NumPunct));
    nl = "\n";
    human_readable_sizes = true;
  }

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher{task_executor.task_runner()};

  const std::unique_ptr<spaced::DiskUsageProxy> disk_usage_proxy =
      spaced::DiskUsageProxy::Generate();

  if (!disk_usage_proxy) {
    LOG(ERROR) << "Failed to get disk usage proxy";
    return EXIT_FAILURE;
  }

  if (!FLAGS_get_free_disk_space.empty()) {
    std::cout << Size(disk_usage_proxy->GetFreeDiskSpace(
                     base::FilePath(FLAGS_get_free_disk_space)))
              << nl;
    return EXIT_SUCCESS;
  }

  if (!FLAGS_get_total_disk_space.empty()) {
    std::cout << Size(disk_usage_proxy->GetTotalDiskSpace(
                     base::FilePath(FLAGS_get_total_disk_space)))
              << nl;
    return EXIT_SUCCESS;
  }

  if (FLAGS_get_root_device_size) {
    std::cout << Size(disk_usage_proxy->GetRootDeviceSize()) << nl;
    return EXIT_SUCCESS;
  }

  if (FLAGS_monitor) {
    std::cout << "Time: " << base::Time::Now() << ", Available space: "
              << Size(disk_usage_proxy->GetFreeDiskSpace(
                     base::FilePath("/mnt/stateful_partition")))
              << std::endl;
    EchoSpacedObserver observer;
    disk_usage_proxy->AddObserver(&observer);
    disk_usage_proxy->StartMonitoring();
    // Infinite loop; let the user interrupt monitoring with Ctrl+C.
    base::RunLoop().Run();
    return EXIT_SUCCESS;
  }

  if (!FLAGS_get_disk_io_stats_for_paths.empty()) {
    std::cout << disk_usage_proxy->GetDiskIOStatsForPathsPrettyPrint(
        FLAGS_get_disk_io_stats_for_paths);
    return EXIT_SUCCESS;
  }

  base::FilePath path(".");
  const base::CommandLine::StringVector args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  if (!args.empty()) {
    if (args.size() > 1) {
      LOG(ERROR) << "Too many command line arguments";
      return EXIT_FAILURE;
    }

    path = base::FilePath(args[0]);
  }

  // Determine full canonical path.
  {
    char* const rp = realpath(path.value().c_str(), nullptr);
    if (!rp) {
      PLOG(ERROR) << "Cannot get real path of " << std::quoted(path.value());
      return EXIT_FAILURE;
    }

    path = base::FilePath(rp);
    free(rp);
  }

  if (FLAGS_get_quota_usage) {
    if (!disk_usage_proxy->IsQuotaSupported(path)) {
      std::cout << "Quota is not supported for path: "
                << std::quoted(path.value()) << '\n';
      return EXIT_FAILURE;
    }
    if (!FLAGS_uid.empty()) {
      uint32_t id = atoi(FLAGS_uid.c_str());
      if (id == 0 && FLAGS_uid != "0") {
        std::cout << "Could not determine the id \n";
        return EXIT_FAILURE;
      }
      std::cout << "uid: " << FLAGS_uid << ": "
                << Size(disk_usage_proxy->GetQuotaCurrentSpaceForUid(path, id))
                << '\n';
      return EXIT_SUCCESS;
    }

    if (!FLAGS_gid.empty()) {
      uint32_t id = atoi(FLAGS_gid.c_str());
      if (id == 0 && FLAGS_gid != "0") {
        std::cout << "Could not determine the id \n";
        return EXIT_FAILURE;
      }
      std::cout << "gid: " << FLAGS_gid << ": "
                << Size(disk_usage_proxy->GetQuotaCurrentSpaceForGid(path, id))
                << '\n';
      return EXIT_SUCCESS;
    }

    if (!FLAGS_project_id.empty()) {
      uint32_t id = atoi(FLAGS_project_id.c_str());
      if (id == 0 && FLAGS_project_id != "0") {
        std::cout << "Could not determine the id \n";
        return EXIT_FAILURE;
      }
      std::cout << "project_id: " << FLAGS_uid << ": "
                << Size(disk_usage_proxy->GetQuotaCurrentSpaceForProjectId(path,
                                                                           id))
                << '\n';
      return EXIT_SUCCESS;
    }
  }

  std::cout << "path: " << std::quoted(path.value()) << '\n';
  std::cout << "free_disk_space: "
            << Size(disk_usage_proxy->GetFreeDiskSpace(path)) << '\n';
  std::cout << "total_disk_space: "
            << Size(disk_usage_proxy->GetTotalDiskSpace(path)) << '\n';
  std::cout << "root_device_size: "
            << Size(disk_usage_proxy->GetRootDeviceSize()) << '\n';
  std::cout << "quota_supported: " << std::boolalpha
            << disk_usage_proxy->IsQuotaSupported(path) << '\n';
  std::cout.flush();

  return EXIT_SUCCESS;
}
