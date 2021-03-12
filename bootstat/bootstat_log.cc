// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of bootstat_log(), part of the Chromium OS 'bootstat'
// facility.

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/stringprintf.h>
#include <brillo/brillo_export.h>
#include <rootdev/rootdev.h>

#include "bootstat/bootstat.h"

namespace bootstat {
//
// Default path to directory where output statistics will be stored.
//
static const char kDefaultOutputDirectoryName[] = "/tmp";

//
// Paths to the statistics files we snapshot as part of the data to
// be logged.
//
static const char kDefaultUptimeStatisticsFileName[] = "/proc/uptime";

// TODO(drinkcat): Cache function output (we only need to evaluate it once)
base::FilePath BootStatSystem::GetDiskStatisticsFilePath() const {
  char boot_path[PATH_MAX];
  int ret = rootdev(boot_path, sizeof(boot_path),
                    true,    // Do full resolution.
                    false);  // Do not remove partition number.
  if (ret < 0)
    return base::FilePath();

  // The general idea is to use the the root device's sysfs entry to
  // get the path to the root disk's sysfs entry.
  // Example:
  // - rootdev() returns "/dev/sda3"
  // - Use /sys/class/block/sda3/../ to get to root disk (sda) sysfs entry.
  //   This is because /sys/class/block/sda3 is a symlink that maps to:
  //     /sys/devices/pci.../.../ata./host./target.../.../block/sda/sda3
  base::FilePath root_device_name = base::FilePath(boot_path).BaseName();

  base::FilePath stat_path = base::FilePath("/sys/class/block")
                                 .Append(root_device_name)
                                 .Append("../stat");

  // Normalize the path as some functions refuse to follow symlink/`..`.
  base::FilePath norm;
  if (!base::NormalizeFilePath(stat_path, &norm))
    return base::FilePath();
  return norm;
}

BootStat::BootStat()
    : BootStat(base::FilePath(kDefaultOutputDirectoryName),
               kDefaultUptimeStatisticsFileName,
               std::make_unique<BootStatSystem>()) {}

BootStat::BootStat(const base::FilePath& output_directory_path,
                   const std::string& uptime_statistics_file_path,
                   std::unique_ptr<BootStatSystem> boot_stat_system)
    : output_directory_path_(output_directory_path),
      uptime_statistics_file_path_(uptime_statistics_file_path),
      boot_stat_system_(std::move(boot_stat_system)) {}

BootStat::~BootStat() = default;

base::ScopedFD BootStat::OpenEventFile(const std::string& output_name_prefix,
                                       const std::string& event_name) const {
  const mode_t kFileCreationMode =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  //
  // For those not up on the more esoteric features of printf
  // formats:  the "%.*s" format is used to truncate the event name
  // to the proper number of characters..
  //
  std::string output_file =
      base::StringPrintf("%s-%.*s", output_name_prefix.c_str(),
                         BOOTSTAT_MAX_EVENT_LEN - 1, event_name.c_str());

  base::FilePath output_path = output_directory_path_.Append(output_file);

  int output_fd = HANDLE_EINTR(open(output_path.value().c_str(),
                                    O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW,
                                    kFileCreationMode));

  return base::ScopedFD(output_fd);
}

bool BootStat::LogDiskEvent(const std::string& event_name) const {
  base::FilePath disk_statistics_file_path =
      boot_stat_system_->GetDiskStatisticsFilePath();

  if (disk_statistics_file_path.empty())
    return false;

  std::string data;
  if (!base::ReadFileToString(disk_statistics_file_path, &data))
    return false;

  base::ScopedFD output_fd = OpenEventFile("disk", event_name);
  if (!output_fd.is_valid())
    return false;

  return base::WriteFileDescriptor(output_fd.get(), data.c_str(), data.size());
}

// TODO(drinkcat): Either merge the common parts of this function with
// LogDiskEvent, or use clock_gettime.
bool BootStat::LogUptimeEvent(const std::string& event_name) const {
  std::string data;
  if (!base::ReadFileToString(base::FilePath(uptime_statistics_file_path_),
                              &data))
    return false;

  base::ScopedFD output_fd = OpenEventFile("uptime", event_name);
  if (!output_fd.is_valid())
    return false;

  return base::WriteFileDescriptor(output_fd.get(), data.c_str(), data.size());
}

// API functions.
bool BootStat::LogEvent(const std::string& event_name) const {
  bool ret = true;

  ret &= LogDiskEvent(event_name);
  ret &= LogUptimeEvent(event_name);

  return ret;
}

};  // namespace bootstat

BRILLO_EXPORT
void bootstat_log(const char* event_name) {
  bootstat::BootStat().LogEvent(event_name);
}
