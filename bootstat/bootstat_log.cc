// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of bootstat_log(), part of the Chromium OS 'bootstat'
// facility.

#include <fcntl.h>
#include <limits.h>
#include <linux/rtc.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <rootdev/rootdev.h>

#include "bootstat/bootstat.h"

namespace bootstat {
//
// Default path to directory where output statistics will be stored.
//
static const char kDefaultOutputDirectoryName[] = "/tmp";

// TODO(drinkcat): Cache function output (we only need to evaluate it once)
base::FilePath BootStatSystem::GetDiskStatisticsFilePath() const {
  char boot_path[PATH_MAX];
  int ret = rootdev(boot_path, sizeof(boot_path),
                    true,    // Do full resolution.
                    false);  // Do not remove partition number.
  if (ret < 0) {
    LOG(ERROR) << "Cannot get rootdev.";
    return base::FilePath();
  }

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
  if (!base::NormalizeFilePath(stat_path, &norm)) {
    LOG(ERROR) << "Cannot normalize disk statistics file path.";
    return base::FilePath();
  }
  return norm;
}

std::optional<struct timespec> BootStatSystem::GetUpTime() const {
  struct timespec uptime;
  int ret = clock_gettime(CLOCK_BOOTTIME, &uptime);
  if (ret != 0) {
    PLOG(ERROR) << "Cannot get uptime (CLOCK_BOOTTIME).";
    return std::nullopt;
  }
  return {uptime};
}

base::ScopedFD BootStatSystem::OpenRtc() const {
  int rtc_fd = HANDLE_EINTR(open("/dev/rtc", O_RDONLY | O_CLOEXEC));
  if (rtc_fd < 0)
    PLOG(ERROR) << "Cannot open RTC";

  return base::ScopedFD(rtc_fd);
}

std::optional<struct rtc_time> BootStatSystem::GetRtcTime(
    base::ScopedFD* rtc_fd) const {
  struct rtc_time rtc_time;
  if (ioctl(rtc_fd->get(), RTC_RD_TIME, &rtc_time) < 0) {
    PLOG(ERROR) << "RTC ioctl error";
    return std::nullopt;
  }

  return {rtc_time};
}

BootStat::BootStat()
    : BootStat(base::FilePath(kDefaultOutputDirectoryName),
               std::make_unique<BootStatSystem>()) {}

BootStat::BootStat(const base::FilePath& output_directory_path,
                   std::unique_ptr<BootStatSystem> boot_stat_system)
    : output_directory_path_(output_directory_path),
      boot_stat_system_(std::move(boot_stat_system)) {}

BootStat::~BootStat() = default;

std::optional<struct BootStat::RtcTick> BootStat::GetRtcTick() const {
  base::ScopedFD rtc_fd = boot_stat_system_->OpenRtc();
  if (!rtc_fd.is_valid())
    return std::nullopt;

  // Record start time so that we can timeout if needed.
  std::optional<struct timespec> tps_start = boot_stat_system_->GetUpTime();
  if (!tps_start)
    return std::nullopt;

  std::optional<struct rtc_time> rtc_time[2];

  for (int i = 0;; i++) {
    int old = (i + 1) % 2;
    int cur = i % 2;

    std::optional<struct timespec> tps_cur = boot_stat_system_->GetUpTime();
    if (!tps_cur)
      return std::nullopt;

    rtc_time[cur] = boot_stat_system_->GetRtcTime(&rtc_fd);
    if (!rtc_time[cur])
      return std::nullopt;

    if (i > 0 && rtc_time[cur]->tm_sec != rtc_time[old]->tm_sec) {
      // RTC ticked, record "after" time.
      std::optional<struct timespec> tps_after = boot_stat_system_->GetUpTime();
      if (!tps_after)
        return std::nullopt;
      return {{*rtc_time[cur], *tps_cur, *tps_after}};
    }

    // Timeout after 1.5 seconds.
    if (difftime(tps_cur->tv_sec, tps_start->tv_sec) +
            (tps_cur->tv_nsec - tps_start->tv_nsec) * 1e-9 >
        1.5) {
      LOG(ERROR) << "Timeout waiting for RTC tick.";
      return std::nullopt;
    }

    // Don't hog the CPU too much, we don't care about sub-ms resolution
    // anyway.
    usleep(1000);
  }
}

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

  int output_fd =
      HANDLE_EINTR(open(output_path.value().c_str(),
                        O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                        kFileCreationMode));

  LOG_IF(ERROR, output_fd < 0)
      << "Cannot open event file " << output_path.value() << ".";

  return base::ScopedFD(output_fd);
}

bool BootStat::LogDiskEvent(const std::string& event_name) const {
  base::FilePath disk_statistics_file_path =
      boot_stat_system_->GetDiskStatisticsFilePath();

  if (disk_statistics_file_path.empty())
    return false;

  std::string data;
  if (!base::ReadFileToString(disk_statistics_file_path, &data)) {
    LOG(ERROR) << "Cannot read disk statistics "
               << disk_statistics_file_path.value() << ".";
    return false;
  }

  base::ScopedFD output_fd = OpenEventFile("disk", event_name);
  if (!output_fd.is_valid())
    return false;

  bool ret = base::WriteFileDescriptor(output_fd.get(), data);
  LOG_IF(ERROR, !ret) << "Cannot write disk event.";
  return ret;
}

bool BootStat::LogUptimeEvent(const std::string& event_name) const {
  std::optional<struct timespec> uptime = boot_stat_system_->GetUpTime();
  if (!uptime)
    return false;

  std::string data = base::StringPrintf("%jd.%09ld\n", (intmax_t)uptime->tv_sec,
                                        uptime->tv_nsec);

  base::ScopedFD output_fd = OpenEventFile("uptime", event_name);
  if (!output_fd.is_valid())
    return false;

  bool ret = base::WriteFileDescriptor(output_fd.get(), data);
  LOG_IF(ERROR, !ret) << "Cannot write uptime event.";
  return ret;
}

// API functions.
bool BootStat::LogEvent(const std::string& event_name) const {
  bool ret = true;

  ret &= LogDiskEvent(event_name);
  ret &= LogUptimeEvent(event_name);

  return ret;
}

bool BootStat::LogRtcSync(const char* event_name) {
  std::optional<struct RtcTick> tick = GetRtcTick();
  if (!tick)
    return false;

  base::ScopedFD output_fd = OpenEventFile("sync-rtc", event_name);
  if (!output_fd.is_valid())
    return false;

  std::string data = base::StringPrintf(
      "%jd.%09ld %jd.%09ld %04d-%02d-%02d %02d:%02d:%02d\n",
      (intmax_t)tick->boottime_before.tv_sec, tick->boottime_before.tv_nsec,
      (intmax_t)tick->boottime_after.tv_sec, tick->boottime_after.tv_nsec,
      tick->rtc_time.tm_year + 1900, tick->rtc_time.tm_mon + 1,
      tick->rtc_time.tm_mday, tick->rtc_time.tm_hour, tick->rtc_time.tm_min,
      tick->rtc_time.tm_sec);

  bool ret = base::WriteFileDescriptor(output_fd.get(), data);
  LOG_IF(ERROR, !ret) << "Cannot write rtc sync.";
  return ret;
}

};  // namespace bootstat
