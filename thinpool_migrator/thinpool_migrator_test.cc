// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/thinpool_migrator.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vpd/fake_vpd.h>

using ::testing::_;
using ::testing::Return;

namespace thinpool_migrator {

class MockThinpoolMigrator : public ThinpoolMigrator {
 public:
  MockThinpoolMigrator()
      : ThinpoolMigrator(
            base::FilePath("/dev/nvme0n1p1"),
            512UL * 1024 * 1024 * 1024,
            std::make_unique<brillo::DeviceMapper>(
                base::BindRepeating(&brillo::fake::CreateDevmapperTask)),
            std::make_unique<vpd::Vpd>(std::make_unique<vpd::FakeVpd>())) {}

  MOCK_METHOD(bool, CheckFilesystemState, (), (override));
  MOCK_METHOD(bool, ReplayExt4Journal, (), (override));
  MOCK_METHOD(bool, ResizeStatefulFilesystem, (uint64_t), (override));

  MOCK_METHOD(bool,
              ConvertThinpoolMetadataToBinary,
              (const base::FilePath&),
              (override));

  MOCK_METHOD(bool, InitializePhysicalVolume, (const std::string&), (override));

  MOCK_METHOD(bool,
              RestoreVolumeGroupConfiguration,
              (const std::string&),
              (override));

  MOCK_METHOD(bool,
              DuplicateHeader,
              (uint64_t, uint64_t, uint64_t),
              (override));

  MOCK_METHOD(bool, IsVpdSupported, (), (override));
};

// Go through the possibilities of migration and ensure that the state machine
// is consistent.
TEST(ThinpoolMigratorTest, NoVpd) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(false));

  EXPECT_FALSE(m.Migrate(false));
}

TEST(ThinpoolMigratorTest, BasicSanity) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_EQ(m.GetState(), MigrationStatus::NOT_STARTED);

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(true));
  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ConvertThinpoolMetadataToBinary(_)).WillOnce(Return(true));
  EXPECT_CALL(m, InitializePhysicalVolume(_)).WillOnce(Return(true));
  EXPECT_CALL(m, RestoreVolumeGroupConfiguration(_)).WillOnce(Return(true));

  EXPECT_TRUE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::COMPLETED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, FailedResize) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(false));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::NOT_STARTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, FailedHeaderCopy) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(true));
  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(false));

  // Revert path
  EXPECT_CALL(m, ResizeStatefulFilesystem(0)).WillOnce(Return(false));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::FILESYSTEM_RESIZED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, FailedThinpoolMetadataPersist) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(true));
  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ConvertThinpoolMetadataToBinary(_)).WillOnce(Return(false));

  // Revert path
  EXPECT_CALL(m, ResizeStatefulFilesystem(0)).WillOnce(Return(false));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::PARTITION_HEADER_COPIED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, FailedPhysicalVolumeInitialization) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(true));
  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ConvertThinpoolMetadataToBinary(_)).WillOnce(Return(true));
  EXPECT_CALL(m, InitializePhysicalVolume(_)).WillOnce(Return(false));

  // Revert path
  EXPECT_CALL(m, DuplicateHeader(_, 0, _)).WillOnce(Return(false));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::THINPOOL_METADATA_PERSISTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, FailedVolumeGroupInitialization) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::NOT_STARTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, CheckFilesystemState()).WillOnce(Return(true));
  EXPECT_CALL(m, ReplayExt4Journal()).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(_)).WillOnce(Return(true));
  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ConvertThinpoolMetadataToBinary(_)).WillOnce(Return(true));
  EXPECT_CALL(m, InitializePhysicalVolume(_)).WillOnce(Return(true));
  EXPECT_CALL(m, RestoreVolumeGroupConfiguration(_)).WillOnce(Return(false));

  // Revert path
  EXPECT_CALL(m, DuplicateHeader(_, 0, _)).WillOnce(Return(false));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::THINPOOL_METADATA_PERSISTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, ZeroTries) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(0);
  CHECK(m.PersistStatus(s));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::NOT_STARTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, ZeroTries_RevertMigration) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(0);
  s.set_state(MigrationStatus::THINPOOL_METADATA_PERSISTED);
  CHECK(m.PersistStatus(s));

  // Revert path
  EXPECT_CALL(m, DuplicateHeader(_, 0, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(0)).WillOnce(Return(true));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::NOT_STARTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, LastTry_RevertMigration) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(1);
  s.set_state(MigrationStatus::THINPOOL_METADATA_PERSISTED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, InitializePhysicalVolume(_)).WillOnce(Return(false));

  // Revert path
  EXPECT_CALL(m, DuplicateHeader(_, 0, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ResizeStatefulFilesystem(0)).WillOnce(Return(true));

  EXPECT_FALSE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::NOT_STARTED);
  EXPECT_EQ(m.GetTries(), 0);
}

TEST(ThinpoolMigratorTest, ResumeInterruptedMigration) {
  MockThinpoolMigrator m;
  EXPECT_CALL(m, IsVpdSupported()).WillRepeatedly(Return(true));

  MigrationStatus s;
  s.set_tries(2);
  s.set_state(MigrationStatus::FILESYSTEM_RESIZED);

  CHECK(m.PersistStatus(s));

  EXPECT_CALL(m, DuplicateHeader(0, _, _)).WillOnce(Return(true));
  EXPECT_CALL(m, ConvertThinpoolMetadataToBinary(_)).WillOnce(Return(true));
  EXPECT_CALL(m, InitializePhysicalVolume(_)).WillOnce(Return(true));
  EXPECT_CALL(m, RestoreVolumeGroupConfiguration(_)).WillOnce(Return(true));

  EXPECT_TRUE(m.Migrate(false));
  EXPECT_EQ(m.GetState(), MigrationStatus::COMPLETED);
  EXPECT_EQ(m.GetTries(), 1);
}

}  // namespace thinpool_migrator
