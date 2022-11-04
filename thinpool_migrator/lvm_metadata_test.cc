// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinpool_migrator/lvm_metadata.h"

#include <string>

#include <base/logging.h>
#include <gtest/gtest.h>

namespace thinpool_migrator {

// Helper for easing readability of string comparisons.
bool IsSubstring(const std::string& needle, const std::string& haystack) {
  return haystack.find(needle) != std::string::npos;
}

// Validate the metadata class' string conversion.
TEST(PhysicalVolumeMetadataTest, BasicSanity) {
  PhysicalVolumeMetadata pv;
  pv.id = "foo";
  pv.device = "/bar/baz";
  pv.status = "ro";
  pv.dev_size = 100;
  pv.pe_start = 0;
  pv.pe_count = 25;

  std::string pv_metadata = pv.ToString(5);
  EXPECT_TRUE(IsSubstring("pv5 {", pv_metadata));
  EXPECT_TRUE(IsSubstring("id = \"foo\"", pv_metadata));
  EXPECT_TRUE(IsSubstring("device = /bar/baz", pv_metadata));
  EXPECT_TRUE(IsSubstring("status = ro", pv_metadata));
  EXPECT_TRUE(IsSubstring("dev_size = 100", pv_metadata));
  EXPECT_TRUE(IsSubstring("pe_start = 0", pv_metadata));
  EXPECT_TRUE(IsSubstring("pe_count = 25", pv_metadata));
}

TEST(VolumeGroupMetadataTest, BasicSanity) {
  VolumeGroupMetadata vg;
  vg.name = "hello";
  vg.id = "foo_vg";
  vg.seqno = 10;
  vg.format = "test";
  vg.status = "rw";
  vg.flags = "rw";
  vg.extent_size = 4096;
  vg.max_lv = 100;
  vg.max_pv = 1;
  vg.metadata_copies = 1;
  vg.creation_time = 1;

  std::string vg_metadata = vg.ToString();
  EXPECT_TRUE(IsSubstring("hello {", vg_metadata));
  EXPECT_TRUE(IsSubstring("id = \"foo_vg\"", vg_metadata));
  EXPECT_TRUE(IsSubstring("seqno = 10", vg_metadata));
  EXPECT_TRUE(IsSubstring("format = \"test\"", vg_metadata));
  EXPECT_TRUE(IsSubstring("status = rw", vg_metadata));
  EXPECT_TRUE(IsSubstring("flags = rw", vg_metadata));
  EXPECT_TRUE(IsSubstring("extent_size = 4096", vg_metadata));
  EXPECT_TRUE(IsSubstring("max_lv = 100", vg_metadata));
  EXPECT_TRUE(IsSubstring("max_pv = 1", vg_metadata));
  EXPECT_TRUE(IsSubstring("metadata_copies = 1", vg_metadata));
  EXPECT_TRUE(IsSubstring("creation_time = 1", vg_metadata));
}

TEST(ThinpoolLogicalVolumeMetadataTest, BasicSanity) {
  LogicalVolumeMetadata lv;

  lv.name = "hello";
  lv.id = "foo_vg";
  lv.status = "rw";
  lv.flags = "rw";
  lv.creation_host = "localhost";
  lv.creation_time = 1;
  lv.segments = {{
      .start_extent = 0,
      .extent_count = 5,
      .type = "thin-pool",
      .thinpool.metadata = "metadev",
      .thinpool.pool = "pooldev",
      .thinpool.transaction_id = 1,
      .thinpool.chunk_size = 4096,
      .thinpool.discards = "true",
      .thinpool.zero_new_blocks = 0,
  }};

  std::string lv_metadata = lv.ToString();
  EXPECT_TRUE(IsSubstring("hello {", lv_metadata));
  EXPECT_TRUE(IsSubstring("id = \"foo_vg\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("status = rw", lv_metadata));
  EXPECT_TRUE(IsSubstring("flags = rw", lv_metadata));
  EXPECT_TRUE(IsSubstring("creation_host = \"localhost\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("creation_time = 1", lv_metadata));
  EXPECT_TRUE(IsSubstring("start_extent = 0", lv_metadata));
  EXPECT_TRUE(IsSubstring("extent_count = 5", lv_metadata));

  EXPECT_TRUE(IsSubstring("type = \"thin-pool\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("metadata = \"metadev\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("pool = \"pooldev\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("chunk_size = 4096", lv_metadata));
  EXPECT_TRUE(IsSubstring("discards = \"true\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("zero_new_blocks = 0", lv_metadata));
}

TEST(ThinLogicalVolumeMetadataTest, BasicSanity) {
  LogicalVolumeMetadata lv;

  lv.name = "hello";
  lv.id = "foo_vg";
  lv.status = "rw";
  lv.flags = "rw";
  lv.creation_host = "localhost";
  lv.creation_time = 1;
  lv.segments = {{.start_extent = 0,
                  .extent_count = 5,
                  .type = "thin",
                  .thin.thin_pool = "thinpool",
                  .thin.transaction_id = 1,
                  .thin.device_id = 5}};

  std::string lv_metadata = lv.ToString();
  EXPECT_TRUE(IsSubstring("hello {", lv_metadata));
  EXPECT_TRUE(IsSubstring("id = \"foo_vg\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("status = rw", lv_metadata));
  EXPECT_TRUE(IsSubstring("flags = rw", lv_metadata));
  EXPECT_TRUE(IsSubstring("creation_host = \"localhost\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("creation_time = 1", lv_metadata));
  EXPECT_TRUE(IsSubstring("start_extent = 0", lv_metadata));
  EXPECT_TRUE(IsSubstring("extent_count = 5", lv_metadata));

  EXPECT_TRUE(IsSubstring("type = \"thin\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("thin_pool = \"thinpool\"", lv_metadata));
  EXPECT_TRUE(IsSubstring("transaction_id = 1", lv_metadata));
  EXPECT_TRUE(IsSubstring("device_id = 5", lv_metadata));
}

TEST(ThinpoolSuperblockMetadataTest, BasicSanity) {
  ThinpoolSuperblockMetadata thinpool_sb_metadata;

  thinpool_sb_metadata.uuid = "foo";
  thinpool_sb_metadata.time = 1;
  thinpool_sb_metadata.transaction = 7;
  thinpool_sb_metadata.flags = 1;
  thinpool_sb_metadata.version = 1;
  thinpool_sb_metadata.data_block_size = 65536;
  thinpool_sb_metadata.nr_data_blocks = 5;

  thinpool_sb_metadata.device_mappings = {
      {.device_id = 1,
       .mapped_blocks = 17,
       .transaction = 8,
       .creation_time = 8,
       .snap_time = 9,
       .mappings = {
           {.type = "single",
            .mapping.single.origin_block = 1,
            .mapping.single.data_block = 5,
            .time = 10},
           {.type = "range",
            .mapping.range.origin_begin = 2,
            .mapping.range.data_begin = 6,
            .mapping.range.length = 10,
            .time = 11},
       }}};

  std::string thinpool_sb_header =
      R"(<superblock uuid="foo" time="1" transaction="7" flags="1" )"
      R"(version="1" data_block_size="65536" nr_data_blocks="5">)";
  std::string thinpool_device_header =
      R"(<device dev_id="1" mapped_blocks="17" transaction="8" )"
      R"(creation_time="8" snap_time="9">)";
  std::string single_mapping =
      R"(<single_mapping origin_block="1" data_block="5" time="10"/>)";
  std::string range_mapping =
      R"(<range_mapping origin_begin="2" data_begin="6" length="10" )"
      R"(time="11"/>)";

  std::string thinpool_metadata = thinpool_sb_metadata.ToString();
  EXPECT_TRUE(IsSubstring(thinpool_sb_header, thinpool_metadata));
  EXPECT_TRUE(IsSubstring(thinpool_device_header, thinpool_metadata));
  EXPECT_TRUE(IsSubstring(single_mapping, thinpool_metadata));
  EXPECT_TRUE(IsSubstring(range_mapping, thinpool_metadata));
}

}  // namespace thinpool_migrator
