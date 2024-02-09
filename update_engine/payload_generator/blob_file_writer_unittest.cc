// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/blob_file_writer.h"

#include <string>

#include <gtest/gtest.h>

#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"

using chromeos_update_engine::test_utils::FillWithData;
using std::string;

namespace chromeos_update_engine {

class BlobFileWriterTest : public ::testing::Test {};

TEST(BlobFileWriterTest, SimpleTest) {
  ScopedTempFile blob_file("BlobFileWriterTest.XXXXXX", true);
  off_t blob_file_size = 0;
  BlobFileWriter blob_file_writer(blob_file.fd(), &blob_file_size);

  const off_t kBlobSize = 1024;
  brillo::Blob blob(kBlobSize);
  FillWithData(&blob);
  EXPECT_EQ(0, blob_file_writer.StoreBlob(blob));
  EXPECT_EQ(kBlobSize, blob_file_writer.StoreBlob(blob));

  brillo::Blob stored_blob(kBlobSize);
  ssize_t bytes_read;
  ASSERT_TRUE(utils::PReadAll(blob_file.fd(), stored_blob.data(), kBlobSize, 0,
                              &bytes_read));
  EXPECT_EQ(bytes_read, kBlobSize);
  EXPECT_EQ(blob, stored_blob);
}

}  // namespace chromeos_update_engine
