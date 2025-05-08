// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/quota.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spaced/disk_usage_impl.h>
#include <spaced/proto_bindings/spaced.pb.h>

extern "C" {
#include <linux/fs.h>
}

using brillo::FakeRunDmStatusIoctl;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace spaced {
namespace {
// ~3% of blocks are allocated.
constexpr const char kSampleReport[] =
    "3 1/24 8/256 - rw discard_passdown "
    "queue_if_no_space - 1024";

constexpr char kQuotaSamplePath[] = "/home/user/chronos";
}  // namespace

class DiskUsageUtilMock : public DiskUsageUtilImpl {
 public:
  DiskUsageUtilMock(struct statvfs st, std::optional<brillo::Thinpool> thinpool)
      : DiskUsageUtilImpl(base::FilePath("/dev/foo"), thinpool),
        st_(st),
        stat_status_(0) {}

  DiskUsageUtilMock(struct statvfs st,
                    std::optional<brillo::Thinpool> thinpool,
                    std::string proc_dir,
                    std::string sys_dev_block_dir)
      : DiskUsageUtilImpl(
            base::FilePath("/dev/foo"), thinpool, proc_dir, sys_dev_block_dir),
        st_(st),
        stat_status_(0) {}

  void set_stat_status(int status) { stat_status_ = status; }

 protected:
  int StatVFS(const base::FilePath& path, struct statvfs* st) override {
    memcpy(st, &st_, sizeof(struct statvfs));
    return !st_.f_fsid;
  }
  int Stat(const std::string& path, struct stat* st) override {
    *st = {};
    st->st_dev = makedev(path.size(), path.size() + 1);
    st->st_mode |= S_IFDIR;
    return stat_status_;
  }

 private:
  struct statvfs st_;
  int stat_status_;
};

TEST(DiskUsageUtilTest, FailedVfsCall) {
  struct statvfs st = {};

  DiskUsageUtilMock disk_usage_mock(st, std::nullopt);
  base::FilePath path("/foo/bar");

  EXPECT_EQ(disk_usage_mock.GetFreeDiskSpace(path), -1);
  EXPECT_EQ(disk_usage_mock.GetTotalDiskSpace(path), -1);
}

TEST(DiskUsageUtilTest, FilesystemData) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 2048;
  st.f_frsize = 4096;

  DiskUsageUtilMock disk_usage_mock(st, std::nullopt);
  base::FilePath path("/foo/bar");

  EXPECT_EQ(disk_usage_mock.GetFreeDiskSpace(path), 4194304);
  EXPECT_EQ(disk_usage_mock.GetTotalDiskSpace(path), 8388608);
}

TEST(DiskUsageUtilTest, ThinProvisionedVolume) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 2048;
  st.f_frsize = 4096;

  auto lvm_command_runner = std::make_shared<brillo::MockLvmCommandRunner>();
  brillo::Thinpool thinpool("thinpool", "STATEFUL", lvm_command_runner);

  DiskUsageUtilMock disk_usage_mock(st, thinpool);
  base::FilePath path("/foo/bar");

  std::vector<std::string> cmd = {"/sbin/dmsetup", "status", "--noflush",
                                  "STATEFUL-thinpool-tpool"};

  auto fn = FakeRunDmStatusIoctl(0, 32768, kSampleReport);
  EXPECT_CALL(*lvm_command_runner.get(), RunDmIoctl(_, _))
      .WillRepeatedly(testing::Invoke(fn));

  // With only 3% of the thinpool occupied, disk usage should use the
  // filesystem data for free space.
  EXPECT_EQ(disk_usage_mock.GetFreeDiskSpace(path), 4194304);
  EXPECT_EQ(disk_usage_mock.GetTotalDiskSpace(path), 8388608);
}

TEST(DiskUsageUtilTest, ThinProvisionedVolumeLowDiskSpace) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 2048;
  st.f_frsize = 4096;

  auto lvm_command_runner = std::make_shared<brillo::MockLvmCommandRunner>();
  brillo::Thinpool thinpool("thinpool", "STATEFUL", lvm_command_runner);

  DiskUsageUtilMock disk_usage_mock(st, thinpool);
  base::FilePath path("/foo/bar");

  std::vector<std::string> cmd = {"/sbin/dmsetup", "status", "--noflush",
                                  "STATEFUL-thinpool-tpool"};

  auto fn = FakeRunDmStatusIoctl(0, 32768, kSampleReport);
  EXPECT_CALL(*lvm_command_runner.get(), RunDmIoctl(_, _))
      .WillRepeatedly(testing::Invoke(fn));

  EXPECT_EQ(disk_usage_mock.GetFreeDiskSpace(path), 4194304);
  EXPECT_EQ(disk_usage_mock.GetTotalDiskSpace(path), 8388608);
}

TEST(DiskUsageUtilTest, OverprovisionedVolumeSpace) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 9192;
  st.f_frsize = 4096;

  auto lvm_command_runner = std::make_shared<brillo::MockLvmCommandRunner>();
  brillo::Thinpool thinpool("thinpool", "STATEFUL", lvm_command_runner);

  DiskUsageUtilMock disk_usage_mock(st, thinpool);
  base::FilePath path("/foo/bar");

  std::vector<std::string> cmd = {"/sbin/dmsetup", "status", "--noflush",
                                  "STATEFUL-thinpool-tpool"};

  auto fn = FakeRunDmStatusIoctl(0, 32768, kSampleReport);
  EXPECT_CALL(*lvm_command_runner.get(), RunDmIoctl(_, _))
      .WillRepeatedly(testing::Invoke(fn));

  EXPECT_EQ(disk_usage_mock.GetFreeDiskSpace(path), 4194304);
  EXPECT_EQ(disk_usage_mock.GetTotalDiskSpace(path), 16777216);
}

class DiskUsageRootdevMock : public DiskUsageUtilImpl {
 public:
  DiskUsageRootdevMock(int64_t size, const base::FilePath& path)
      : DiskUsageUtilImpl(path, std::nullopt),
        rootdev_size_(size),
        rootdev_path_(path) {}

 protected:
  int64_t GetBlockDeviceSize(const base::FilePath& device) override {
    // At the moment, only the root device size is queried from spaced.
    // Once more block devices are queried, move this into a map.
    if (device == rootdev_path_) {
      return rootdev_size_;
    }

    return -1;
  }

 private:
  int64_t rootdev_size_;
  base::FilePath rootdev_path_;
};

TEST(DiskUsageUtilTest, InvalidRootDeviceTest) {
  DiskUsageRootdevMock disk_usage_mock(0, base::FilePath("/dev/foo"));

  EXPECT_EQ(disk_usage_mock.GetRootDeviceSize(), 0);
}

TEST(DiskUsageUtilTest, RootDeviceSizeTest) {
  DiskUsageRootdevMock disk_usage_mock(500, base::FilePath("/dev/foo"));

  EXPECT_EQ(disk_usage_mock.GetRootDeviceSize(), 500);
}

class DiskUsageQuotaMock : public DiskUsageUtilImpl {
 public:
  explicit DiskUsageQuotaMock(const base::FilePath& home_device)
      : DiskUsageUtilImpl(base::FilePath("/dev/foo"), std::nullopt),
        home_device_(home_device) {}

  void set_current_space_for_uid(uint32_t uid, uint64_t space) {
    uid_to_current_space_[uid] = space;
  }
  void set_current_space_for_gid(uint32_t gid, uint64_t space) {
    gid_to_current_space_[gid] = space;
  }
  void set_current_space_for_project_id(uint32_t project_id, uint64_t space) {
    project_id_to_current_space_[project_id] = space;
  }

  void set_uids(std::vector<uid_t> uids) { uids_ = uids; }

  void set_gids(std::vector<gid_t> gids) { gids_ = gids; }

  void set_project_ids(std::vector<uint32_t> project_ids) {
    project_ids_ = project_ids;
  }

  int Ioctl(int fd, uint32_t request, void* ptr) override {
    switch (request) {
      case FS_IOC_FSGETXATTR: {
        auto iter = fd_to_project_id_.find(fd);
        if (iter == fd_to_project_id_.end()) {
          (reinterpret_cast<struct fsxattr*>(ptr))->fsx_projid = 0;
          return 0;
        }
        (reinterpret_cast<struct fsxattr*>(ptr))->fsx_projid = iter->second;
        return 0;
      }
      case FS_IOC_FSSETXATTR:
        fd_to_project_id_[fd] =
            (reinterpret_cast<struct fsxattr*>(ptr))->fsx_projid;
        return 0;
      case FS_IOC_GETFLAGS: {
        auto iter = fd_to_ext_flags_.find(fd);
        if (iter == fd_to_ext_flags_.end()) {
          *(reinterpret_cast<int*>(ptr)) = 0;
          return 0;
        }
        *(reinterpret_cast<int*>(ptr)) = iter->second;
        return 0;
      }
      case FS_IOC_SETFLAGS:
        fd_to_ext_flags_[fd] = *(reinterpret_cast<int*>(ptr));
        return 0;
      default:
        LOG(ERROR) << "Unsupported request in ioctl: " << request;
        return -1;
    }
  }

 protected:
  base::FilePath GetDevice(const base::FilePath& path) override {
    return home_device_;
  }

  int QuotaCtl(int cmd,
               const base::FilePath& device,
               int id,
               struct dqblk* dq) override {
    dq->dqb_curspace = 0;
    std::map<uint32_t, uint64_t>* current_space_map;
    if (cmd == QCMD(Q_GETQUOTA, USRQUOTA)) {
      current_space_map = &uid_to_current_space_;
    } else if (cmd == QCMD(Q_GETQUOTA, GRPQUOTA)) {
      current_space_map = &gid_to_current_space_;
    } else if (cmd == QCMD(Q_GETQUOTA, PRJQUOTA)) {
      current_space_map = &project_id_to_current_space_;
    } else {
      return -1;
    }
    auto iter = current_space_map->find(id);
    if (iter == current_space_map->end()) {
      return -1;
    }
    dq->dqb_curspace = iter->second;
    return 0;
  }

  std::vector<uid_t> GetUsers() override { return uids_; }

  std::vector<gid_t> GetGroups() override { return gids_; }

  std::vector<uint32_t> GetProjectIds() override { return project_ids_; }

 private:
  base::FilePath home_device_;
  std::map<uint32_t, uint64_t> uid_to_current_space_;
  std::map<uint32_t, uint64_t> gid_to_current_space_;
  std::map<uint32_t, uint64_t> project_id_to_current_space_;
  std::map<int, int> fd_to_project_id_;
  std::map<int, int> fd_to_ext_flags_;
  std::vector<uint32_t> uids_;
  std::vector<uint32_t> gids_;
  std::vector<uint32_t> project_ids_;
};

TEST(DiskUsageUtilTest, QuotaNotSupportedWhenPathNotMounted) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath(""));
  base::FilePath path(kQuotaSamplePath);

  EXPECT_FALSE(disk_usage_mock.IsQuotaSupported(path));
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 0), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 1), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForGid(path, 2), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForProjectId(path, 3), -1);

  GetQuotaCurrentSpacesForIdsReply reply =
      disk_usage_mock.GetQuotaCurrentSpacesForIds(path, {1, 2}, {10, 11},
                                                  {100, 101});
  EXPECT_TRUE(reply.curspaces_for_uids().empty());
  EXPECT_TRUE(reply.curspaces_for_gids().empty());
  EXPECT_TRUE(reply.curspaces_for_project_ids().empty());

  disk_usage_mock.set_uids({1, 2});
  disk_usage_mock.set_gids({10, 11});
  disk_usage_mock.set_project_ids({100, 101});

  GetQuotaCurrentSpacesForIdsReply overall_reply =
      disk_usage_mock.GetQuotaOverallUsage(path);
  EXPECT_TRUE(reply.curspaces_for_uids().empty());
  EXPECT_TRUE(reply.curspaces_for_gids().empty());
  EXPECT_TRUE(reply.curspaces_for_project_ids().empty());

  std::string str_reply = disk_usage_mock.GetQuotaOverallUsagePrettyPrint(path);
  EXPECT_EQ(str_reply, "Users:\n\nGroups:\n\nProjects:\n");
}

TEST(DiskUsageUtilTest, QuotaNotSupportedWhenPathNotMountedWithQuotaOption) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath("/dev/bar"));
  base::FilePath path(kQuotaSamplePath);

  // Current disk space for UID 0 is undefined.
  EXPECT_FALSE(disk_usage_mock.IsQuotaSupported(path));
}

TEST(DiskUsageUtilTest, QuotaSupported) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath("/dev/bar"));
  base::FilePath path(kQuotaSamplePath);
  disk_usage_mock.set_current_space_for_uid(0, 1);
  disk_usage_mock.set_current_space_for_uid(1, 10);
  disk_usage_mock.set_current_space_for_gid(10, 20);
  disk_usage_mock.set_current_space_for_project_id(100, 30);

  EXPECT_TRUE(disk_usage_mock.IsQuotaSupported(path));
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 1), 10);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 2), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForGid(path, 10), 20);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForGid(path, 11), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForProjectId(path, 100), 30);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForProjectId(path, 101), -1);
  GetQuotaCurrentSpacesForIdsReply reply =
      disk_usage_mock.GetQuotaCurrentSpacesForIds(path, {1, 2}, {10, 11},
                                                  {100, 101});
  EXPECT_EQ(reply.curspaces_for_uids().count(0), 0);
  EXPECT_EQ(reply.curspaces_for_uids().at(1), 10);
  EXPECT_EQ(reply.curspaces_for_uids().at(2), -1);
  EXPECT_EQ(reply.curspaces_for_gids().count(1), 0);
  EXPECT_EQ(reply.curspaces_for_gids().at(10), 20);
  EXPECT_EQ(reply.curspaces_for_gids().at(11), -1);
  EXPECT_EQ(reply.curspaces_for_project_ids().count(2), 0);
  EXPECT_EQ(reply.curspaces_for_project_ids().at(100), 30);
  EXPECT_EQ(reply.curspaces_for_project_ids().at(101), -1);

  disk_usage_mock.set_uids({1, 2});
  disk_usage_mock.set_gids({10, 11});
  disk_usage_mock.set_project_ids({100, 101});

  GetQuotaCurrentSpacesForIdsReply overall_reply =
      disk_usage_mock.GetQuotaOverallUsage(path);
  EXPECT_EQ(reply.curspaces_for_uids().count(0), 0);
  EXPECT_EQ(reply.curspaces_for_uids().at(1), 10);
  EXPECT_EQ(reply.curspaces_for_uids().at(2), -1);
  EXPECT_EQ(reply.curspaces_for_gids().count(1), 0);
  EXPECT_EQ(reply.curspaces_for_gids().at(10), 20);
  EXPECT_EQ(reply.curspaces_for_gids().at(11), -1);
  EXPECT_EQ(reply.curspaces_for_project_ids().count(2), 0);
  EXPECT_EQ(reply.curspaces_for_project_ids().at(100), 30);
  EXPECT_EQ(reply.curspaces_for_project_ids().at(101), -1);

  std::string str_reply = disk_usage_mock.GetQuotaOverallUsagePrettyPrint(path);
  std::string users =
      str_reply.substr(str_reply.find("Users"), str_reply.find("Groups"));
  std::string groups =
      str_reply.substr(str_reply.find("Groups"), str_reply.find("Projects"));
  std::string projects = str_reply.substr(str_reply.find("Projects"));

  EXPECT_NE(users.find("1: 10"), std::string::npos);
  EXPECT_NE(users.find("2: -1"), std::string::npos);
  EXPECT_NE(groups.find("10: 20"), std::string::npos);
  EXPECT_NE(groups.find("11: -1"), std::string::npos);
  EXPECT_NE(projects.find("100: 30"), std::string::npos);
  EXPECT_NE(projects.find("101: -1"), std::string::npos);
}

TEST(DiskUsageUtilTest, SetProjectId) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath("/dev/foo"));
  const base::ScopedFD fd(open("/dev/null", O_RDONLY));
  constexpr int kProjectId = 1003;

  // Set the project ID.
  int error = 0;
  ASSERT_TRUE(disk_usage_mock.SetProjectId(fd, kProjectId, &error));

  // Verify that the fd has the expected project ID.
  struct fsxattr fsx_out = {};
  ASSERT_EQ(disk_usage_mock.Ioctl(fd.get(), FS_IOC_FSGETXATTR, &fsx_out), 0);
  EXPECT_EQ(fsx_out.fsx_projid, kProjectId);
}

TEST(DiskUsageUtilTest, SetProjectInheritanceFlag) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath("/dev/foo"));
  const base::ScopedFD fd(open("/dev/null", O_RDONLY));
  constexpr int kOriginalFlags = FS_ENCRYPT_FL | FS_EXTENT_FL;
  int old_flags = kOriginalFlags;

  // Set FS_ENCRYPT_FL and FS_EXTENT_FL to the file.
  ASSERT_EQ(disk_usage_mock.Ioctl(fd.get(), FS_IOC_SETFLAGS, &old_flags), 0);

  // Set the project inheritance flag.
  int error = 0;
  ASSERT_TRUE(
      disk_usage_mock.SetProjectInheritanceFlag(fd, true /* enable */, &error));

  // Check that the flag is enabled and the original flags are preserved.
  int flags = 0;
  ASSERT_EQ(disk_usage_mock.Ioctl(fd.get(), FS_IOC_GETFLAGS, &flags), 0);
  EXPECT_EQ(flags, kOriginalFlags | FS_PROJINHERIT_FL);

  // Unset the project inheritance flag and check the flags.
  ASSERT_TRUE(disk_usage_mock.SetProjectInheritanceFlag(fd, false /* enable */,
                                                        &error));
  flags = 0;
  ASSERT_EQ(disk_usage_mock.Ioctl(fd.get(), FS_IOC_GETFLAGS, &flags), 0);
  EXPECT_EQ(flags, kOriginalFlags);
}

TEST(DiskUsageUtilTest, GetDiskIOStats) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath disk_stats_path = temp_dir.GetPath().Append("diskstats");
  brillo::WriteToFile(disk_stats_path, "1 2 3", 5);

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt,
                                    temp_dir.GetPath().value(), "");

  std::string str_reply = disk_usage_mock.GetDiskIOStats();
  EXPECT_EQ(str_reply, "\nI/O stats for all block devices:\n1 2 3");
}

TEST(DiskUsageUtilTest, GetDiskIOStatsWithFailedFileRead) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // The diskstats file is left uncreated

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt,
                                    temp_dir.GetPath().value(), "");

  std::string str_reply = disk_usage_mock.GetDiskIOStats();
  EXPECT_EQ(str_reply, "");
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForPaths) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt, "",
                                    temp_dir.GetPath().value());

  base::FilePath dir67 = temp_dir.GetPath().Append("6:7");
  ASSERT_TRUE(base::CreateDirectory(dir67));
  base::FilePath file67 = dir67.Append("stat");
  std::string contents67 = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17";
  brillo::WriteToFile(file67, contents67.c_str(), contents67.size());

  base::FilePath dir78 = temp_dir.GetPath().Append("7:8");
  ASSERT_TRUE(base::CreateDirectory(dir78));
  base::FilePath file78 = dir78.Append("stat");
  std::string contents78 =
      "11 21 31 41 51 61 71 81 91 101 111 121 131 141 151 161 171";
  brillo::WriteToFile(file78, contents78.c_str(), contents78.size());

  GetDiskIOStatsForPathsReply reply = disk_usage_mock.GetDiskIOStatsForPaths(
      {base::FilePath("/test1"), base::FilePath("/test12")});
  EXPECT_EQ(reply.stats_for_path().size(), 2);
  EXPECT_EQ(reply.stats_for_path().at(0).path(), "/test1");
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_ios(), 1);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_merges(), 2);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_sectors(), 3);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_ticks(), 4);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_ios(), 5);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_merges(), 6);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_sectors(), 7);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_ticks(), 8);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().in_flight(), 9);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().io_ticks(), 10);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().time_in_queue(), 11);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_ios(), 12);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_merges(), 13);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_sectors(), 14);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_ticks(), 15);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().flush_ios(), 16);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().flush_ticks(), 17);
  EXPECT_EQ(reply.stats_for_path().at(1).path(), "/test12");
  EXPECT_EQ(reply.stats_for_path().at(1).stats().read_ios(), 11);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().read_merges(), 21);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().read_sectors(), 31);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().read_ticks(), 41);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().write_ios(), 51);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().write_merges(), 61);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().write_sectors(), 71);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().write_ticks(), 81);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().in_flight(), 91);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().io_ticks(), 101);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().time_in_queue(), 111);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().discard_ios(), 121);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().discard_merges(), 131);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().discard_sectors(), 141);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().discard_ticks(), 151);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().flush_ios(), 161);
  EXPECT_EQ(reply.stats_for_path().at(1).stats().flush_ticks(), 171);
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForPathsWithFailedFileRead) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // The stat files are left uncreated

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt, "",
                                    temp_dir.GetPath().value());

  GetDiskIOStatsForPathsReply reply = disk_usage_mock.GetDiskIOStatsForPaths(
      {base::FilePath("/test1"), base::FilePath("/test12")});
  EXPECT_EQ(reply.stats_for_path().size(), 0);
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForPathsWithFailedStat) {
  DiskUsageUtilMock disk_usage_mock({}, std::nullopt);

  // Simulate a stat() failure
  disk_usage_mock.set_stat_status(1);

  GetDiskIOStatsForPathsReply reply = disk_usage_mock.GetDiskIOStatsForPaths(
      {base::FilePath("/test1"), base::FilePath("/test12")});
  EXPECT_EQ(reply.stats_for_path().size(), 0);
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForPathsWithFailedFileParsing) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt, "",
                                    temp_dir.GetPath().value());

  base::FilePath dir67 = temp_dir.GetPath().Append("6:7");
  ASSERT_TRUE(base::CreateDirectory(dir67));
  base::FilePath file67 = dir67.Append("stat");
  // Insufficient number of entries: 16 rather than 17
  std::string contents67 = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16";
  brillo::WriteToFile(file67, contents67.c_str(), contents67.size());

  base::FilePath dir78 = temp_dir.GetPath().Append("7:8");
  ASSERT_TRUE(base::CreateDirectory(dir78));
  base::FilePath file78 = dir78.Append("stat");
  std::string contents78 =
      "11 21 31 41 51 61 71 81 91 101 111 121 131 141 151 161 171";
  brillo::WriteToFile(file78, contents78.c_str(), contents78.size());

  GetDiskIOStatsForPathsReply reply = disk_usage_mock.GetDiskIOStatsForPaths(
      {base::FilePath("/test1"), base::FilePath("/test12")});
  EXPECT_EQ(reply.stats_for_path().size(), 1);
  EXPECT_EQ(reply.stats_for_path().at(0).path(), "/test12");
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_ios(), 11);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_merges(), 21);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_sectors(), 31);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().read_ticks(), 41);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_ios(), 51);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_merges(), 61);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_sectors(), 71);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().write_ticks(), 81);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().in_flight(), 91);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().io_ticks(), 101);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().time_in_queue(), 111);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_ios(), 121);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_merges(), 131);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_sectors(), 141);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().discard_ticks(), 151);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().flush_ios(), 161);
  EXPECT_EQ(reply.stats_for_path().at(0).stats().flush_ticks(), 171);
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForPathsPrettyPrint) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt, "",
                                    temp_dir.GetPath().value());

  base::FilePath dir67 = temp_dir.GetPath().Append("6:7");
  ASSERT_TRUE(base::CreateDirectory(dir67));
  base::FilePath file67 = dir67.Append("stat");
  std::string contents67 = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17";
  brillo::WriteToFile(file67, contents67.c_str(), contents67.size());

  base::FilePath dir78 = temp_dir.GetPath().Append("7:8");
  ASSERT_TRUE(base::CreateDirectory(dir78));
  base::FilePath file78 = dir78.Append("stat");
  std::string contents78 =
      "11 21 31 41 51 61 71 81 91 101 111 121 131 141 151 161 171";
  brillo::WriteToFile(file78, contents78.c_str(), contents78.size());

  std::string str_reply =
      disk_usage_mock.GetDiskIOStatsForPathsPrettyPrint("/test1,/test12");
  EXPECT_EQ(str_reply,
            "\nDisk I/O stats for /test1:\n"
            "Read IOs: 1\n"
            "Read Merges: 2\n"
            "Read Sectors: 3\n"
            "Read Ticks: 4\n"
            "Writes IOs: 5\n"
            "Write Merges: 6\n"
            "Write Sectors: 7\n"
            "Write Ticks: 8\n"
            "In Flight: 9\n"
            "IO Ticks: 10\n"
            "Time In Queue: 11\n"
            "Discard IOs: 12\n"
            "Discard Merges: 13\n"
            "Discard Sectors: 14\n"
            "Discard Ticks: 15\n"
            "Flush IOs: 16\n"
            "Flush Ticks: 17\n"
            "\nDisk I/O stats for /test12:\n"
            "Read IOs: 11\n"
            "Read Merges: 21\n"
            "Read Sectors: 31\n"
            "Read Ticks: 41\n"
            "Writes IOs: 51\n"
            "Write Merges: 61\n"
            "Write Sectors: 71\n"
            "Write Ticks: 81\n"
            "In Flight: 91\n"
            "IO Ticks: 101\n"
            "Time In Queue: 111\n"
            "Discard IOs: 121\n"
            "Discard Merges: 131\n"
            "Discard Sectors: 141\n"
            "Discard Ticks: 151\n"
            "Flush IOs: 161\n"
            "Flush Ticks: 171\n");

  // Verify elimination of duplicates (i.e. paths  that point to the same
  // block device)
  str_reply =
      disk_usage_mock.GetDiskIOStatsForPathsPrettyPrint("/test1,/test2");
  EXPECT_EQ(str_reply,
            "\nDisk I/O stats for /test1:\n"
            "Read IOs: 1\n"
            "Read Merges: 2\n"
            "Read Sectors: 3\n"
            "Read Ticks: 4\n"
            "Writes IOs: 5\n"
            "Write Merges: 6\n"
            "Write Sectors: 7\n"
            "Write Ticks: 8\n"
            "In Flight: 9\n"
            "IO Ticks: 10\n"
            "Time In Queue: 11\n"
            "Discard IOs: 12\n"
            "Discard Merges: 13\n"
            "Discard Sectors: 14\n"
            "Discard Ticks: 15\n"
            "Flush IOs: 16\n"
            "Flush Ticks: 17\n");
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForWithFailedStat) {
  DiskUsageUtilMock disk_usage_mock({}, std::nullopt);

  // Simulate a stat() failure
  disk_usage_mock.set_stat_status(1);

  std::string str_reply =
      disk_usage_mock.GetDiskIOStatsForPathsPrettyPrint("/test1,/test12");
  EXPECT_EQ(str_reply, "");
}

TEST(DiskUsageUtilTest, GetDiskIOStatsForWithFailedFileRead) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // The stat file is left uncreated

  DiskUsageUtilMock disk_usage_mock({}, std::nullopt, "",
                                    temp_dir.GetPath().value());

  std::string str_reply =
      disk_usage_mock.GetDiskIOStatsForPathsPrettyPrint("/test");
  EXPECT_EQ(str_reply, "");
}

}  // namespace spaced
