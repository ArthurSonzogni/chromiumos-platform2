// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of bootstat_log(), part of the Chromium OS 'bootstat'
// facility.

#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

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
std::string BootStatSystem::GetDiskStatisticsFileName() const {
  char boot_path[PATH_MAX];
  int ret = rootdev(boot_path, sizeof(boot_path),
                    true,    // Do full resolution.
                    false);  // Do not remove partition number.
  if (ret < 0)
    return std::string();

  // The general idea is to use the the root device's sysfs entry to
  // get the path to the root disk's sysfs entry.
  // Example:
  // - rootdev() returns "/dev/sda3"
  // - Use /sys/class/block/sda3/../ to get to root disk (sda) sysfs entry.
  //   This is because /sys/class/block/sda3 is a symlink that maps to:
  //     /sys/devices/pci.../.../ata./host./target.../.../block/sda/sda3
  const char* root_device_name = basename(boot_path);
  if (!root_device_name)
    return std::string();

  char stats_path[PATH_MAX];
  ret = snprintf(stats_path, sizeof(stats_path), "/sys/class/block/%s/../stat",
                 root_device_name);
  if (ret >= sizeof(stats_path) || ret < 0)
    return std::string();
  return stats_path;
}

BootStat::BootStat()
    : BootStat(kDefaultOutputDirectoryName,
               kDefaultUptimeStatisticsFileName,
               std::make_unique<BootStatSystem>()) {}

BootStat::BootStat(const std::string& output_directory_path,
                   const std::string& uptime_statistics_file_path,
                   std::unique_ptr<BootStatSystem> boot_stat_system)
    : output_directory_path_(output_directory_path),
      uptime_statistics_file_path_(uptime_statistics_file_path),
      boot_stat_system_(std::move(boot_stat_system)) {}

BootStat::~BootStat() = default;

int BootStat::OpenEventFile(const std::string& output_name_prefix,
                            const std::string& event_name) const {
  const mode_t kFileCreationMode =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  //
  // For those not up on the more esoteric features of printf
  // formats:  the "%.*s" format is used to truncate the event name
  // to the proper number of characters..
  //
  char output_path[PATH_MAX];
  int output_path_len =
      snprintf(output_path, sizeof(output_path), "%s/%s-%.*s",
               output_directory_path_.c_str(), output_name_prefix.c_str(),
               BOOTSTAT_MAX_EVENT_LEN - 1, event_name.c_str());
  if (output_path_len >= sizeof(output_path))
    return -1;

  // TODO(drinkcat): Handle EINTR or use libchrome's HANDLE_EINTR.
  int output_fd = open(output_path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW,
                       kFileCreationMode);

  // TODO(drinkcat): Replace with base::ScopedFD
  return output_fd;
}

static bool CopyFromFile(const std::string& input_file_name, int output_fd) {
  int ifd = open(input_file_name.c_str(), O_RDONLY);
  if (ifd < 0)
    return false;

  char buffer[256];
  ssize_t num_read;
  // TODO(drinkcat): Handle EINTR or use libchrome's HANDLE_EINTR.
  while ((num_read = read(ifd, buffer, sizeof(buffer))) > 0) {
    ssize_t num_written = write(output_fd, buffer, num_read);
    // TODO(drinkcat): Partial write should be retried.
    if (num_written != num_read)
      break;
  }
  (void)close(ifd);

  return true;
}

bool BootStat::LogDiskEvent(const std::string& event_name) const {
  std::string disk_statistics_file_name =
      boot_stat_system_->GetDiskStatisticsFileName();

  if (disk_statistics_file_name.empty())
    return false;

  int output_fd = OpenEventFile("disk", event_name);
  if (output_fd < 0)
    return false;

  bool ret = CopyFromFile(disk_statistics_file_name, output_fd);

  close(output_fd);
  return ret;
}

bool BootStat::LogUptimeEvent(const std::string& event_name) const {
  int output_fd = OpenEventFile("uptime", event_name);
  if (output_fd < 0)
    return false;

  bool ret = CopyFromFile(uptime_statistics_file_path_, output_fd);

  close(output_fd);
  return ret;
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
