// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "update_engine/full_update_generator.h"
#include "update_engine/test_utils.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

namespace {
  const size_t kBlockSize = 4096;
}  // namespace {}


class FullUpdateGeneratorTest : public ::testing::Test { };


TEST(FullUpdateGeneratorTest, RunTest) {
  vector<char> new_root(20 * 1024 * 1024);
  vector<char> new_kern(16 * 1024 * 1024);
  const off_t kChunkSize = 128 * 1024;
  FillWithData(&new_root);
  FillWithData(&new_kern);
  // Assume hashes take 2 MiB beyond the rootfs.
  off_t new_rootfs_size = new_root.size() - 2 * 1024 * 1024;

  string new_root_path;
  EXPECT_TRUE(utils::MakeTempFile("/tmp/NewFullUpdateTest_R.XXXXXX",
                                  &new_root_path,
                                  NULL));
  ScopedPathUnlinker new_root_path_unlinker(new_root_path);
  EXPECT_TRUE(WriteFileVector(new_root_path, new_root));

  string new_kern_path;
  EXPECT_TRUE(utils::MakeTempFile("/tmp/NewFullUpdateTest_K.XXXXXX",
                                  &new_kern_path,
                                  NULL));
  ScopedPathUnlinker new_kern_path_unlinker(new_kern_path);
  EXPECT_TRUE(WriteFileVector(new_kern_path, new_kern));

  string out_blobs_path;
  int out_blobs_fd;
  EXPECT_TRUE(utils::MakeTempFile("/tmp/NewFullUpdateTest_D.XXXXXX",
                                  &out_blobs_path,
                                  &out_blobs_fd));
  ScopedPathUnlinker out_blobs_path_unlinker(out_blobs_path);
  ScopedFdCloser out_blobs_fd_closer(&out_blobs_fd);

  off_t out_blobs_length = 0;

  Graph graph;
  vector<DeltaArchiveManifest_InstallOperation> kernel_ops;
  vector<Vertex::Index> final_order;

  EXPECT_TRUE(FullUpdateGenerator::Run(&graph,
                                       new_kern_path,
                                       new_root_path,
                                       new_rootfs_size,
                                       out_blobs_fd,
                                       &out_blobs_length,
                                       kChunkSize,
                                       kBlockSize,
                                       &kernel_ops,
                                       &final_order));
  EXPECT_EQ(new_rootfs_size / kChunkSize, graph.size());
  EXPECT_EQ(new_rootfs_size / kChunkSize, final_order.size());
  EXPECT_EQ(new_kern.size() / kChunkSize, kernel_ops.size());
  for (off_t i = 0; i < (new_rootfs_size / kChunkSize); ++i) {
    EXPECT_EQ(i, final_order[i]);
    EXPECT_EQ(1, graph[i].op.dst_extents_size());
    EXPECT_EQ(i * kChunkSize / kBlockSize,
              graph[i].op.dst_extents(0).start_block()) << "i = " << i;
    EXPECT_EQ(kChunkSize / kBlockSize,
              graph[i].op.dst_extents(0).num_blocks());
    if (graph[i].op.type() !=
        DeltaArchiveManifest_InstallOperation_Type_REPLACE) {
      EXPECT_EQ(DeltaArchiveManifest_InstallOperation_Type_REPLACE_BZ,
                graph[i].op.type());
    }
  }
}

}  // namespace chromeos_update_engine
