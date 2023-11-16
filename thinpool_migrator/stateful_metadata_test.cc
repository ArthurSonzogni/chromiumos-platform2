// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/stateful_metadata.h"

#include <string>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <gtest/gtest.h>

namespace thinpool_migrator {
namespace {
const uint64_t kGigabyte = 1024 * 1024 * 1024;

uint64_t GetStripeLocation(LogicalVolumeMetadata& metadata) {
  CHECK_EQ(metadata.segments[0].type, "striped");

  return metadata.segments[0].stripe.stripes["pv0"];
}

uint64_t GetExtentCount(LogicalVolumeMetadata& metadata) {
  return metadata.segments[0].extent_count;
}

}  // namespace

class StatefulMetadataTest : public StatefulMetadata,
                             public ::testing::Test,
                             public ::testing::WithParamInterface<uint64_t> {
 public:
  StatefulMetadataTest()
      : StatefulMetadata(base::FilePath("/dev/nvme0n1p1"), GetParam()),
        device_size_(GetParam()) {}

 protected:
  void SetUp() override {}
  void TearDown() override {}

  uint64_t device_size_;
};

TEST_P(StatefulMetadataTest, ValidateExtents) {
  uint64_t total_extent_count = GetTotalExtentCount();
  uint64_t thinpool_data_extent_count = GetThinpoolExtentCount();
  uint64_t thinpool_metadata_extent_count = GetThinpoolMetadataExtentCount();
  uint64_t unencrypted_stateful_extents = GetUnencryptedStatefulExtentCount();
  uint64_t relocated_stateful_header_location =
      GetRelocatedStatefulHeaderLocation();

  // Validate that the thinpool covers the entire partition.
  EXPECT_EQ(total_extent_count,
            2 * thinpool_metadata_extent_count + thinpool_data_extent_count);

  // Validate that the stateful header is relocated to the last extent of the
  // unencrypted stateful partition.
  EXPECT_EQ(unencrypted_stateful_extents,
            relocated_stateful_header_location + 1);
}

TEST_P(StatefulMetadataTest, ValidatePhysicalVolume) {
  PhysicalVolumeMetadata pv = GeneratePhysicalVolumeMetadata();

  EXPECT_EQ(pv.pe_start, 2 * 1024);
  EXPECT_EQ(pv.pe_count, (device_size_ - 1024 * 1024) / (4 * 1024 * 1024));
  EXPECT_EQ(pv.dev_size, device_size_ / 512);
}

TEST_P(StatefulMetadataTest, ValidateLogicalVolumes) {
  std::vector<LogicalVolumeMetadata> lv_metadata =
      GenerateLogicalVolumeMetadata();

  EXPECT_EQ(lv_metadata.size(), 5);

  // The logical volumes are always generated in order. Validate individually.
  LogicalVolumeMetadata thinpool = lv_metadata[0];
  LogicalVolumeMetadata unencrypted = lv_metadata[1];
  LogicalVolumeMetadata tpool_spare = lv_metadata[2];
  LogicalVolumeMetadata tpool_metadata = lv_metadata[3];
  LogicalVolumeMetadata tpool_data = lv_metadata[4];

  // Make sure that the ordering of the thick logical volumes for data,
  // metadata and spare is consistent.
  EXPECT_EQ(GetStripeLocation(tpool_data), 0);
  EXPECT_EQ(GetStripeLocation(tpool_metadata), GetExtentCount(tpool_data));
  EXPECT_EQ(GetStripeLocation(tpool_spare),
            GetExtentCount(tpool_data) + GetExtentCount(tpool_metadata));

  // Expect the unencrypted stateful logical volume to be smaller than the
  // thinpool.
  EXPECT_LE(GetExtentCount(unencrypted), GetExtentCount(thinpool));
}

TEST_P(StatefulMetadataTest, ValidateThinpoolMappings) {
  ThinpoolSuperblockMetadata thinpool = GenerateThinpoolSuperblockMetadata();

  EXPECT_EQ(thinpool.data_block_size, 128);
  EXPECT_EQ(thinpool.nr_data_blocks, GetUnencryptedStatefulExtentCount() * 64);

  EXPECT_EQ(thinpool.device_mappings.size(), 1);
  EXPECT_EQ(thinpool.device_mappings[0].mappings.size(), 2);

  ThinBlockMapping header = thinpool.device_mappings[0].mappings[0];
  ThinBlockMapping body = thinpool.device_mappings[0].mappings[1];

  EXPECT_EQ(header.mapping.range.origin_begin, 0);
  EXPECT_EQ(header.mapping.range.data_begin,
            GetRelocatedStatefulHeaderLocation() * 64);
  EXPECT_EQ(header.mapping.range.length, 16);
  EXPECT_EQ(body.mapping.range.origin_begin, 16);
  EXPECT_EQ(body.mapping.range.data_begin, 0);
  EXPECT_EQ(body.mapping.range.length,
            GetRelocatedStatefulHeaderLocation() * 64);
}

// The test suite is initialized with 2^n - 8 GB to approximate for 4GB root
// partitions.
INSTANTIATE_TEST_SUITE_P(StatefulMetadataSuite,
                         StatefulMetadataTest,
                         ::testing::Values(8 * kGigabyte,
                                           24 * kGigabyte,
                                           56 * kGigabyte,
                                           120 * kGigabyte,
                                           248 * kGigabyte,
                                           504 * kGigabyte,
                                           1016 * kGigabyte,
                                           2040 * kGigabyte));

}  // namespace thinpool_migrator
