// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <spaced/disk_usage_impl.h>

#include <fcntl.h>
#include <sys/quota.h>
#include <sys/statvfs.h>

#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/mock_lvm.h>

extern "C" {
#include <linux/fs.h>
}

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace spaced {
namespace {
// ~3% of blocks are allocated.
constexpr const char kSampleReport[] =
    "0 32768 thin-pool 3 1/24 8/256 - rw discard_passdown "
    "queue_if_no_space - 1024";

constexpr char kQuotaSamplePath[] = "/home/user/chronos";
}  // namespace

class DiskUsageUtilMock : public DiskUsageUtilImpl {
 public:
  DiskUsageUtilMock(struct statvfs st, std::optional<brillo::Thinpool> thinpool)
      : DiskUsageUtilImpl(base::FilePath("/dev/foo"), thinpool), st_(st) {}

 protected:
  int StatVFS(const base::FilePath& path, struct statvfs* st) override {
    memcpy(st, &st_, sizeof(struct statvfs));
    return !st_.f_fsid;
  }

 private:
  struct statvfs st_;
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

  std::string report = kSampleReport;
  EXPECT_CALL(*lvm_command_runner.get(), RunProcess(cmd, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(report), Return(true)));

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

  std::string report = kSampleReport;
  EXPECT_CALL(*lvm_command_runner.get(), RunProcess(cmd, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(report), Return(true)));

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

  std::string report = kSampleReport;
  EXPECT_CALL(*lvm_command_runner.get(), RunProcess(cmd, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(report), Return(true)));

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
    if (device == rootdev_path_)
      return rootdev_size_;

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

 private:
  base::FilePath home_device_;
  std::map<uint32_t, uint64_t> uid_to_current_space_;
  std::map<uint32_t, uint64_t> gid_to_current_space_;
  std::map<uint32_t, uint64_t> project_id_to_current_space_;
  std::map<int, int> fd_to_project_id_;
  std::map<int, int> fd_to_ext_flags_;
};

TEST(DiskUsageUtilTest, QuotaNotSupportedWhenPathNotMounted) {
  DiskUsageQuotaMock disk_usage_mock(base::FilePath(""));
  base::FilePath path(kQuotaSamplePath);

  EXPECT_FALSE(disk_usage_mock.IsQuotaSupported(path));
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 0), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForUid(path, 1), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForGid(path, 2), -1);
  EXPECT_EQ(disk_usage_mock.GetQuotaCurrentSpaceForProjectId(path, 3), -1);
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

}  // namespace spaced
