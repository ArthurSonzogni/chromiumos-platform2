// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/partition_migrator.h"

#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "installer/cgpt_manager.h"
#include "installer/mock_cgpt_manager.h"

using testing::_;

namespace installer {

namespace {

// Helper function to create a Partition object.
Partition CreatePartition(int number,
                          const std::string& label,
                          uint64_t start,
                          uint64_t size,
                          Guid type = GPT_ENT_TYPE_BASIC_DATA) {
  return Partition{
      .number = number,
      .label = label,
      .start = start,
      .size = size,
      .type = type,
  };
}

}  // namespace

TEST(PartitionMigratorTest, InitializePartitionMetadata_AddAtBeginning) {
  // Add two new partitions at the beginning of the reclaimed partition.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "foo_a", 0, 128),
      CreatePartition(14, "foo_b", 0, 128),
  });

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 0, 1024),
      /*new_partitions=*/
      std::move(new_partitions),
      /*relabeled_partitions=*/std::vector<Partition>({}),
      std::move(cgpt_manager));

  migrator->InitializePartitionMetadata();

  // Verify the updated partition metadata.
  auto reclaimed_partition = migrator->get_reclaimed_partition_for_test();
  new_partitions = migrator->get_new_partitions_for_test();

  EXPECT_EQ(reclaimed_partition.start, 256);
  EXPECT_EQ(reclaimed_partition.size, 768);
  EXPECT_EQ(new_partitions[0].start, 0);
  EXPECT_EQ(new_partitions[0].size, 128);
  EXPECT_EQ(new_partitions[1].start, 128);
  EXPECT_EQ(new_partitions[1].size, 128);
}

TEST(PartitionMigratorTest, InitializePartitionMetadata_AddAtEnd) {
  // Add two new partitions at the end of the reclaimed partition.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "foo_a", 0, 128),
      CreatePartition(14, "foo_b", 0, 128),
  });

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/true,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 0, 1024),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::vector<Partition>({}),
      std::move(cgpt_manager));

  migrator->InitializePartitionMetadata();

  // Verify the updated partition metadata.
  auto reclaimed_partition = migrator->get_reclaimed_partition_for_test();
  new_partitions = migrator->get_new_partitions_for_test();

  // Verify the updated partition metadata.
  EXPECT_EQ(reclaimed_partition.start, 0);
  EXPECT_EQ(reclaimed_partition.size, 768);
  EXPECT_EQ(new_partitions[0].start, 768);
  EXPECT_EQ(new_partitions[0].size, 128);
  EXPECT_EQ(new_partitions[1].start, 896);
  EXPECT_EQ(new_partitions[1].size, 128);
}

TEST(PartitionMigratorTest, RevertPartitionMetadata) {
  // Add two new partitions at the beginning of the reclaimed partition.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "foo_a", 0, 128),
      CreatePartition(14, "foo_b", 0, 128),
  });

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 0, 1024),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::vector<Partition>({}),
      std::move(cgpt_manager));

  migrator->InitializePartitionMetadata();
  migrator->RevertPartitionMetadata();

  // Verify the updated partition metadata.
  auto reclaimed_partition = migrator->get_reclaimed_partition_for_test();

  // Verify the reverted partition metadata.
  EXPECT_EQ(reclaimed_partition.start, 0);
  EXPECT_EQ(reclaimed_partition.size, 1024);
}

TEST(PartitionMigratorTest, ReclaimAndAddNewPartitions) {
  // Add two new partitions at the beginning of the reclaimed partition.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "foo_a", 0, 128),
      CreatePartition(14, "foo_b", 0, 128),
  });

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager,
              SetSectorRange(PartitionNum(3), std::optional<uint64_t>(256),
                             std::optional<uint64_t>(768)))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(13), 0, 128, "foo_a", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager,
              AddPartition(PartitionNum(14), 128, 128, "foo_b", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 0, 1024),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::vector<Partition>({}),
      std::move(cgpt_manager));

  // Run the migration.
  EXPECT_TRUE(migrator->RunMigration());
}

TEST(PartitionMigratorTest, RemoveNewPartitionsAndClaim) {
  // Check that the revert path reclaims the space in the existing partition.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "foo_a", 0, 128),
      CreatePartition(14, "foo_b", 0, 128),
  });

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(13), 0, 128, "foo_a", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(14), 0, 128, "foo_b", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager,
              SetSectorRange(PartitionNum(3), std::optional<uint64_t>(0),
                             std::optional<uint64_t>(1024)))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 256, 768),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::vector<Partition>({}),
      std::move(cgpt_manager));

  // Revert the migration.
  migrator->RevertMigration();
}

TEST(PartitionMigratorTest, RelabelExistingPartitions) {
  // Check that existing partitions get relabeled.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  auto relabeled_partitions = std::vector<Partition>({
      CreatePartition(1, "xyz", 0, 1024),
      CreatePartition(3, "abcd", 0, 1024),
  });

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(1), "xyz"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(3), "abcd"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  // Run the migration.
  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 256, 768),
      /*new_partitions=*/std::vector<Partition>({}),
      /*relabeled_partitions=*/std::move(relabeled_partitions),
      std::move(cgpt_manager));

  EXPECT_TRUE(migrator->RunMigration());
}

TEST(PartitionMigratorTest, UndoPartitionRelabel) {
  // Check that the old label is reset during the revert path.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(1), "STATE"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(3), "ROOT-A"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  // Run the migration.
  std::vector<Partition> relabeled_partitions = {
      CreatePartition(1, "xyz", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
      CreatePartition(3, "abcd", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
  };
  relabeled_partitions[0].old_label = "STATE";
  relabeled_partitions[1].old_label = "ROOT-A";

  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 256, 768),
      /*new_partitions=*/std::vector<Partition>({}),
      /*relabeled_partitions=*/std::move(relabeled_partitions),
      std::move(cgpt_manager));

  migrator->RevertMigration();
}

TEST(PartitionMigratorTest, RunMigration) {
  // Run partition migration end-to-end.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager,
              SetSectorRange(PartitionNum(3), std::optional<uint64_t>(256),
                             std::optional<uint64_t>(768)))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(13), 0, 128, "xyz", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager,
              AddPartition(PartitionNum(14), 128, 128, "abcd", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(1), "foobar"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(3), "foobaz"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  // Run the migration.
  auto new_partitions = std::vector<Partition>({
      CreatePartition(13, "xyz", 0, 128),
      CreatePartition(14, "abcd", 0, 128),
  });

  auto relabeled_partitions = std::vector<Partition>({
      CreatePartition(1, "foobar", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
      CreatePartition(3, "foobaz", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
  });
  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 0, 1024),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::move(relabeled_partitions),
      std::move(cgpt_manager));

  EXPECT_TRUE(migrator->RunMigration());
}

TEST(PartitionMigratorTest, RevertMigration) {
  // Revert full migration.
  std::unique_ptr<MockCgptManager> cgpt_manager =
      std::make_unique<MockCgptManager>();

  std::vector<Partition> new_partitions = {
      CreatePartition(13, "xyz", 0, 128),
      CreatePartition(14, "abcd", 0, 128),
  };

  std::vector<Partition> relabeled_partitions = {
      CreatePartition(1, "foobar", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
      CreatePartition(3, "foobaz", 0, 1024, GPT_ENT_TYPE_LINUX_FS),
  };
  relabeled_partitions[0].old_label = "STATE";
  relabeled_partitions[1].old_label = "ROOT-A";

  // Set up expectations for the CgptManager calls.
  EXPECT_CALL(*cgpt_manager,
              SetSectorRange(PartitionNum(3), std::optional<uint64_t>(0),
                             std::optional<uint64_t>(1024)))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(13), 0, 128, "xyz", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, AddPartition(PartitionNum(14), 0, 128, "abcd", _))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(1), "STATE"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));
  EXPECT_CALL(*cgpt_manager, SetLabel(PartitionNum(3), "ROOT-A"))
      .WillOnce(::testing::Return(CgptErrorCode::kSuccess));

  // Run the migration.
  auto migrator = std::make_unique<PartitionMigrator>(
      /*add_at_end=*/false,
      /*reclaimed_partition=*/CreatePartition(3, "ROOT-A", 256, 768),
      /*new_partitions=*/std::move(new_partitions),
      /*relabeled_partitions=*/std::move(relabeled_partitions),
      std::move(cgpt_manager));

  migrator->RevertMigration();
}

}  // namespace installer
