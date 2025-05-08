// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/disk_usage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/quota.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/blkdev_utils/get_backing_block_device.h>
#include <brillo/userdb_utils.h>
#include <rootdev/rootdev.h>
#include <spaced/proto_bindings/spaced.pb.h>

extern "C" {
#include <linux/fs.h>
}

namespace spaced {

constexpr char kProjectIdJson[] = "/etc/spaced/projects.json";

constexpr char kProcPrefix[] = "/proc";
constexpr char kDiskstatsFilename[] = "diskstats";
constexpr char kSysDevBlockPrefix[] = "/sys/dev/block";
constexpr char kStatFilename[] = "stat";

constexpr int kNumIOStatsEntries = 17;

DiskUsageUtilImpl::DiskUsageUtilImpl(const base::FilePath& rootdev,
                                     std::optional<brillo::Thinpool> thinpool)
    : rootdev_(rootdev),
      thinpool_(thinpool),
      proc_dir_(kProcPrefix),
      sys_dev_block_dir_(kSysDevBlockPrefix) {}

DiskUsageUtilImpl::DiskUsageUtilImpl(const base::FilePath& rootdev,
                                     std::optional<brillo::Thinpool> thinpool,
                                     std::string proc_dir,
                                     std::string sys_dev_block_dir)
    : rootdev_(rootdev),
      thinpool_(thinpool),
      proc_dir_(proc_dir),
      sys_dev_block_dir_(sys_dev_block_dir) {}

int DiskUsageUtilImpl::StatVFS(const base::FilePath& path, struct statvfs* st) {
  return HANDLE_EINTR(statvfs(path.value().c_str(), st));
}

int DiskUsageUtilImpl::Stat(const std::string& path, struct stat* st) {
  return HANDLE_EINTR(stat(path.c_str(), st));
}

int DiskUsageUtilImpl::QuotaCtl(int cmd,
                                const base::FilePath& device,
                                int id,
                                struct dqblk* dq) {
  return quotactl(cmd, device.value().c_str(), id, reinterpret_cast<char*>(dq));
}

int DiskUsageUtilImpl::Ioctl(int fd, uint32_t request, void* ptr) {
  return ioctl(fd, request, ptr);
}

base::FilePath DiskUsageUtilImpl::GetDevice(const base::FilePath& path) {
  return brillo::GetBackingLogicalDeviceForFile(path);
}

int64_t DiskUsageUtilImpl::GetFreeDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the free space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    PLOG(ERROR) << "Failed to run statvfs() on " << path;
    return -1;
  }

  int64_t free_disk_space = static_cast<int64_t>(stat.f_bavail) * stat.f_frsize;

  return free_disk_space;
}

int64_t DiskUsageUtilImpl::GetTotalDiskSpace(const base::FilePath& path) {
  // Use statvfs() to get the total space for the given path.
  struct statvfs stat;

  if (StatVFS(path, &stat) != 0) {
    PLOG(ERROR) << "Failed to run statvfs() on " << path;
    return -1;
  }

  int64_t total_disk_space =
      static_cast<int64_t>(stat.f_blocks) * stat.f_frsize;

  int64_t thinpool_total_space;
  if (thinpool_ && thinpool_->IsValid() &&
      thinpool_->GetTotalSpace(&thinpool_total_space)) {
    total_disk_space = std::min(total_disk_space, thinpool_total_space);
  }

  return total_disk_space;
}

int64_t DiskUsageUtilImpl::GetBlockDeviceSize(const base::FilePath& device) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(device.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return -1;
  }

  int64_t size;
  if (Ioctl(fd.get(), BLKGETSIZE64, &size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE): " << device.value();
    return -1;
  }
  return size;
}

int64_t DiskUsageUtilImpl::GetRootDeviceSize() {
  if (rootdev_.empty()) {
    LOG(WARNING) << "Failed to get root device";
    return -1;
  }

  return GetBlockDeviceSize(rootdev_);
}

bool DiskUsageUtilImpl::IsQuotaSupported(const base::FilePath& path) {
  return GetQuotaCurrentSpaceForUid(path, 0) >= 0;
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForUid(
    const base::FilePath& path, uint32_t uid) {
  return GetQuotaCurrentSpaceForId(path, uid, USRQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForGid(
    const base::FilePath& path, uint32_t gid) {
  return GetQuotaCurrentSpaceForId(path, gid, GRPQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForProjectId(
    const base::FilePath& path, uint32_t project_id) {
  return GetQuotaCurrentSpaceForId(path, project_id, PRJQUOTA);
}

int64_t DiskUsageUtilImpl::GetQuotaCurrentSpaceForId(const base::FilePath& path,
                                                     uint32_t id,
                                                     int quota_type) {
  DCHECK(0 <= quota_type && quota_type < MAXQUOTAS)
      << "Invalid quota_type: " << quota_type;

  const base::FilePath device = GetDevice(path);
  if (device.empty()) {
    LOG(ERROR) << "Failed to find logical device for home directory";
    return -1;
  }

  struct dqblk dq = {};
  if (QuotaCtl(QCMD(Q_GETQUOTA, quota_type), device, id, &dq) != 0) {
    PLOG(ERROR) << "quotactl failed: quota_type=" << quota_type << ", id=" << id
                << ", device=" << device.value();
    return -1;
  }
  return dq.dqb_curspace;
}

GetQuotaCurrentSpacesForIdsReply DiskUsageUtilImpl::GetQuotaCurrentSpacesForIds(
    const base::FilePath& path,
    const std::vector<uint32_t>& uids,
    const std::vector<uint32_t>& gids,
    const std::vector<uint32_t>& project_ids) {
  GetQuotaCurrentSpacesForIdsReply reply;
  const base::FilePath device = GetDevice(path);
  if (device.empty()) {
    LOG(ERROR) << "Failed to find logical device for home directory";
    return reply;
  }
  SetQuotaCurrentSpacesForIdsMap(device, uids, USRQUOTA,
                                 reply.mutable_curspaces_for_uids());
  SetQuotaCurrentSpacesForIdsMap(device, gids, GRPQUOTA,
                                 reply.mutable_curspaces_for_gids());
  SetQuotaCurrentSpacesForIdsMap(device, project_ids, PRJQUOTA,
                                 reply.mutable_curspaces_for_project_ids());
  return reply;
}

void DiskUsageUtilImpl::SetQuotaCurrentSpacesForIdsMap(
    const base::FilePath& device,
    const std::vector<uint32_t>& ids,
    int quota_type,
    google::protobuf::Map<uint32_t, int64_t>* curspaces_for_ids) {
  DCHECK(0 <= quota_type && quota_type <= MAXQUOTAS)
      << "Invalid quota_type: " << quota_type;
  for (const auto& id : ids) {
    struct dqblk dq = {};
    if (QuotaCtl(QCMD(Q_GETQUOTA, quota_type), device, id, &dq) != 0) {
      PLOG(ERROR) << "quotactl(GETQUOTA) failed: quota_type=" << quota_type
                  << ", id=" << id << ", device=" << device;
      (*curspaces_for_ids)[id] = -1;
    } else {
      (*curspaces_for_ids)[id] = dq.dqb_curspace;
    }
  }
}

std::vector<uid_t> DiskUsageUtilImpl::GetUsers() {
  return brillo::userdb::GetUsers();
}

std::vector<gid_t> DiskUsageUtilImpl::GetGroups() {
  return brillo::userdb::GetGroups();
}

std::vector<uint32_t> DiskUsageUtilImpl::GetProjectIds() {
  std::vector<uint32_t> project_ids;
  std::string json_string;
  if (!base::ReadFileToString(base::FilePath(kProjectIdJson), &json_string)) {
    PLOG(ERROR) << "Unable to read json file: " << kProjectIdJson;
    return project_ids;
  }
  auto prj =
      base::JSONReader::Read(json_string, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!prj) {
    LOG(ERROR) << "Failed to read json file";
    return project_ids;
  }
  if (!prj->is_dict()) {
    LOG(ERROR) << "Failed to read json file as a dictionary";
    return project_ids;
  }

  base::Value::List* projects = prj->GetDict().FindList("projects");
  if (projects == nullptr) {
    LOG(ERROR) << "Failed to get project ids";
    return project_ids;
  }

  for (const auto& prj : *projects) {
    if (!prj.is_dict()) {
      LOG(ERROR) << "Failed to get project information";
      continue;
    }
    const auto& prj_dict = prj.GetDict();
    const std::string* value = prj_dict.FindString("id");
    const std::string* name = prj_dict.FindString("name");
    if (value != nullptr && name != nullptr) {
      int num = 0;
      CHECK(base::StringToInt(*value, &num));
      project_ids.push_back(num);
      projects_[num] = *name;
    }
  }
  return project_ids;
}

GetQuotaCurrentSpacesForIdsReply DiskUsageUtilImpl::GetQuotaOverallUsage(
    const base::FilePath& path) {
  GetQuotaCurrentSpacesForIdsReply reply;

  std::vector<uid_t> users = GetUsers();
  std::vector<gid_t> groups = GetGroups();
  std::vector<uint32_t> projects = GetProjectIds();

  reply = GetQuotaCurrentSpacesForIds(path, users, groups, projects);
  return reply;
}

std::string DiskUsageUtilImpl::GetQuotaOverallUsagePrettyPrint(
    const base::FilePath& path) {
  GetQuotaCurrentSpacesForIdsReply reply;
  std::string output;

  reply = GetQuotaOverallUsage(path);
  output.append("Users:\n");
  for (auto const& usr : reply.curspaces_for_uids()) {
    if (usr.second != 0) {
      std::string s =
          std::to_string(usr.first) + ": " + std::to_string(usr.second) + "\n";
      output.append(s);
    }
  }
  output.append("\nGroups:\n");
  for (auto const& grp : reply.curspaces_for_gids()) {
    if (grp.second != 0) {
      std::string s =
          std::to_string(grp.first) + ": " + std::to_string(grp.second) + "\n";
      output.append(s);
    }
  }
  output.append("\nProjects:\n");
  for (auto const& prj : reply.curspaces_for_project_ids()) {
    if (prj.second != 0) {
      std::string s =
          std::to_string(prj.first) + ": " + std::to_string(prj.second) + "\n";
      output.append(s);
    }
  }
  return output;
}

std::map<std::pair<uint32_t, uint32_t>, std::string>
DiskUsageUtilImpl::GetDeviceMap(const std::vector<base::FilePath>& paths) {
  // Use stat() to get the major/minor numbers for the specified path.
  struct stat stat;

  std::map<std::pair<uint32_t, uint32_t>, std::string> dev_map;
  for (auto const& path : paths) {
    if (Stat(path.value(), &stat) != 0) {
      PLOG(ERROR) << "Failed to run stat() on:" << path;
      continue;
    }

    if (!S_ISDIR(stat.st_mode)) {
      PLOG(ERROR) << path << " is not a directory";
      continue;
    }

    dev_t dev_num = stat.st_dev;
    unsigned int major_num = major(dev_num);
    unsigned int minor_num = minor(dev_num);
    if (dev_map.count({major_num, minor_num})) {
      PLOG(WARNING) << "Skipping duplicate entry: " << path;
      continue;
    }
    dev_map[{major_num, minor_num}] = path.value();
  }
  return dev_map;
}

void DiskUsageUtilImpl::ParseDiskIOStatsAndUpdateReply(
    const std::string& name,
    const std::string& stats,
    GetDiskIOStatsForPathsReply* reply) {
  std::stringstream stats_stream(stats);
  std::vector<uint64_t> stats_list;
  uint64_t value;
  while (stats_stream >> value) {
    stats_list.push_back(value);
  }
  if (stats_list.size() != kNumIOStatsEntries) {
    PLOG(ERROR) << "Unable to parse I/O stats file for " << name;
    return;
  }
  auto reply_entry = reply->add_stats_for_path();
  *reply_entry->mutable_path() = name;
  int index = 0;
  reply_entry->mutable_stats()->set_read_ios(stats_list[index++]);
  reply_entry->mutable_stats()->set_read_merges(stats_list[index++]);
  reply_entry->mutable_stats()->set_read_sectors(stats_list[index++]);
  reply_entry->mutable_stats()->set_read_ticks(stats_list[index++]);
  reply_entry->mutable_stats()->set_write_ios(stats_list[index++]);
  reply_entry->mutable_stats()->set_write_merges(stats_list[index++]);
  reply_entry->mutable_stats()->set_write_sectors(stats_list[index++]);
  reply_entry->mutable_stats()->set_write_ticks(stats_list[index++]);
  reply_entry->mutable_stats()->set_in_flight(stats_list[index++]);
  reply_entry->mutable_stats()->set_io_ticks(stats_list[index++]);
  reply_entry->mutable_stats()->set_time_in_queue(stats_list[index++]);
  reply_entry->mutable_stats()->set_discard_ios(stats_list[index++]);
  reply_entry->mutable_stats()->set_discard_merges(stats_list[index++]);
  reply_entry->mutable_stats()->set_discard_sectors(stats_list[index++]);
  reply_entry->mutable_stats()->set_discard_ticks(stats_list[index++]);
  reply_entry->mutable_stats()->set_flush_ios(stats_list[index++]);
  reply_entry->mutable_stats()->set_flush_ticks(stats_list[index++]);
}

GetDiskIOStatsForPathsReply DiskUsageUtilImpl::GetDiskIOStatsForPaths(
    const std::vector<base::FilePath>& paths) {
  // Map each specified path to the corresponding device major:minor numbers.
  std::map<std::pair<uint32_t, uint32_t>, std::string> dev_map =
      GetDeviceMap(paths);

  GetDiskIOStatsForPathsReply reply;
  for (const auto& p : dev_map) {
    unsigned int major_num = p.first.first;
    unsigned int minor_num = p.first.second;
    const std::string& name = p.second;

    std::string sysfspath =
        base::StringPrintf("%s/%d:%d/%s", sys_dev_block_dir_.c_str(), major_num,
                           minor_num, kStatFilename);

    std::string stats;
    if (!base::ReadFileToString(base::FilePath(sysfspath), &stats)) {
      PLOG(ERROR) << "Unable to read sysfs file: " << sysfspath;
      continue;
    }

    ParseDiskIOStatsAndUpdateReply(name, stats, &reply);
  }
  return reply;
}

std::string DiskUsageUtilImpl::GetDiskIOStatsForPathsPrettyPrint(
    const std::string& paths) {
  std::vector<base::FilePath> file_paths;
  std::stringstream path_stream(paths);
  std::string path;
  while (std::getline(path_stream, path, ',')) {
    file_paths.push_back(base::FilePath(path));
  }

  GetDiskIOStatsForPathsReply reply = GetDiskIOStatsForPaths(file_paths);
  std::string output;
  std::stringstream stats_stream;
  for (auto const& entry : reply.stats_for_path()) {
    stats_stream
        << std::endl
        << "Disk I/O stats for " << entry.path() << ":" << std::endl
        << "Read IOs: " << entry.stats().read_ios() << std::endl
        << "Read Merges: " << entry.stats().read_merges() << std::endl
        << "Read Sectors: " << entry.stats().read_sectors() << std::endl
        << "Read Ticks: " << entry.stats().read_ticks() << std::endl
        << "Writes IOs: " << entry.stats().write_ios() << std::endl
        << "Write Merges: " << entry.stats().write_merges() << std::endl
        << "Write Sectors: " << entry.stats().write_sectors() << std::endl
        << "Write Ticks: " << entry.stats().write_ticks() << std::endl
        << "In Flight: " << entry.stats().in_flight() << std::endl
        << "IO Ticks: " << entry.stats().io_ticks() << std::endl
        << "Time In Queue: " << entry.stats().time_in_queue() << std::endl
        << "Discard IOs: " << entry.stats().discard_ios() << std::endl
        << "Discard Merges: " << entry.stats().discard_merges() << std::endl
        << "Discard Sectors: " << entry.stats().discard_sectors() << std::endl
        << "Discard Ticks: " << entry.stats().discard_ticks() << std::endl
        << "Flush IOs: " << entry.stats().flush_ios() << std::endl
        << "Flush Ticks: " << entry.stats().flush_ticks() << std::endl;
    output.append(stats_stream.str());
    stats_stream.str("");
  }
  return output;
}

std::string DiskUsageUtilImpl::GetDiskIOStats() {
  std::string output;

  std::string diskstats_path =
      base::StringPrintf("%s/%s", proc_dir_.c_str(), kDiskstatsFilename);
  if (!base::ReadFileToString(base::FilePath(diskstats_path), &output)) {
    PLOG(ERROR) << "Unable to read diskstats file: " << diskstats_path;
    return "";
  }

  output.insert(0, "\nI/O stats for all block devices:\n");

  return output;
}

bool DiskUsageUtilImpl::SetProjectId(const base::ScopedFD& fd,
                                     uint32_t project_id,
                                     int* out_error) {
  if (!fd.is_valid()) {
    *out_error = EBADF;
    LOG(ERROR) << "SetProjectId: Invalid fd";
    return false;
  }

  struct fsxattr fsx = {};
  if (Ioctl(fd.get(), FS_IOC_FSGETXATTR, &fsx) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_FSGETXATTR) failed";
    return false;
  }
  fsx.fsx_projid = project_id;
  if (Ioctl(fd.get(), FS_IOC_FSSETXATTR, &fsx) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_FSSETXATTR) failed: project_id=" << project_id;
    return false;
  }
  return true;
}

bool DiskUsageUtilImpl::SetProjectInheritanceFlag(const base::ScopedFD& fd,
                                                  bool enable,
                                                  int* out_error) {
  if (!fd.is_valid()) {
    *out_error = EBADF;
    LOG(ERROR) << "SetProjectInheritanceFlag: Invalid fd";
    return false;
  }

  uint32_t flags = 0;
  if (Ioctl(fd.get(), FS_IOC_GETFLAGS, &flags) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_GETFLAGS) failed";
    return false;
  }

  if (enable) {
    flags |= FS_PROJINHERIT_FL;
  } else {
    flags &= ~FS_PROJINHERIT_FL;
  }

  if (Ioctl(fd.get(), FS_IOC_SETFLAGS, reinterpret_cast<void*>(&flags)) < 0) {
    *out_error = errno;
    PLOG(ERROR) << "ioctl(FS_IOC_SETFLAGS) failed: flags=" << std::hex << flags;
    return false;
  }
  return true;
}

}  // namespace spaced
