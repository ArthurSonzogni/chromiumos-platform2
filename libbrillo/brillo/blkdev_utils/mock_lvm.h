// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_
#define LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_

#include <linux/dm-ioctl.h>

#include <brillo/process/process_mock.h>
#include <gmock/gmock.h>

#include "brillo/blkdev_utils/lvm.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

using testing::_;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace brillo {

class MockLvmCommandRunner : public LvmCommandRunner {
 public:
  MockLvmCommandRunner() : LvmCommandRunner() {
    ON_CALL(*this, RunCommand(_)).WillByDefault(Return(true));
    ON_CALL(*this, RunProcess(_, _)).WillByDefault(Return(true));
    ON_CALL(*this, RunDmIoctl(_, _)).WillByDefault(Return(false));
  }

  virtual ~MockLvmCommandRunner() {}

  MOCK_METHOD(bool, RunCommand, (const std::vector<std::string>&), (override));
  MOCK_METHOD(bool,
              RunProcess,
              (const std::vector<std::string>&, std::string*),
              (override));
  // Disable int linter - argument to libc function.
  // NOLINTNEXTLINE: (runtime/int)
  MOCK_METHOD(bool, RunDmIoctl, (unsigned long, struct dm_ioctl*), (override));
};

static inline std::function<bool(int, struct dm_ioctl*)> FakeRunDmStatusIoctl(
    uint32_t sector_start, uint32_t length, const std::string& status) {
  // Disable int linter - argument to libc function.
  // NOLINTNEXTLINE: (runtime/int)
  return [sector_start, length, status](unsigned long ioctl_num,
                                        struct dm_ioctl* param) -> bool {
    struct dm_target_spec spec = {.sector_start = sector_start,
                                  .length = length,
                                  .target_type = "thin-pool"};
    char* buf = reinterpret_cast<char*>(param);

    memcpy(buf + param->data_start, &spec, sizeof(struct dm_target_spec));
    // NOLINTNEXTLINE (runtime/printf)
    strcpy(buf + param->data_start + sizeof(struct dm_target_spec),
           status.c_str());
    param->data_size =
        param->data_start + sizeof(struct dm_target_spec) + status.length() + 1;

    return true;
  };
}

class MockLogicalVolumeManager : public LogicalVolumeManager {
 public:
  MockLogicalVolumeManager()
      : LogicalVolumeManager(std::make_shared<MockLvmCommandRunner>()) {}
  virtual ~MockLogicalVolumeManager() {}

  MOCK_METHOD(std::optional<PhysicalVolume>,
              GetPhysicalVolume,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(std::optional<VolumeGroup>,
              GetVolumeGroup,
              (const PhysicalVolume&),
              (override));
  MOCK_METHOD(std::optional<Thinpool>,
              GetThinpool,
              (const VolumeGroup&, const std::string&),
              (override));
  MOCK_METHOD(std::optional<LogicalVolume>,
              GetLogicalVolume,
              (const VolumeGroup&, const std::string&),
              (override));
  MOCK_METHOD(std::vector<LogicalVolume>,
              ListLogicalVolumes,
              (const VolumeGroup&, const std::string&),
              (override));

  MOCK_METHOD(std::optional<PhysicalVolume>,
              CreatePhysicalVolume,
              (const base::FilePath&),
              (override));
  MOCK_METHOD(std::optional<VolumeGroup>,
              CreateVolumeGroup,
              (const PhysicalVolume&, const std::string&),
              (override));
  MOCK_METHOD(std::optional<Thinpool>,
              CreateThinpool,
              (const VolumeGroup&, const base::Value&),
              (override));
  MOCK_METHOD(std::optional<LogicalVolume>,
              CreateLogicalVolume,
              (const VolumeGroup&, const Thinpool&, const base::Value::Dict&),
              (override));
  MOCK_METHOD(bool,
              RemoveLogicalVolume,
              (const VolumeGroup&, const std::string&),
              (override));
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_BLKDEV_UTILS_MOCK_LVM_H_
