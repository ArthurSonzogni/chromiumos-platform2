// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "patchmaker/compression_util.h"

static const char test_data[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate ";

TEST(Compression, CompressionEfficacy) {
  static const brillo::Blob original_data = brillo::BlobFromString(test_data);
  brillo::Blob compressed_data;

  // Confirm that compressed data is smaller than the original data
  ASSERT_TRUE(util::Compress(original_data, &compressed_data));
  ASSERT_TRUE(compressed_data != original_data);
  ASSERT_TRUE(compressed_data.size() < original_data.size());
}

TEST(Compression, ReversibleCompression) {
  static const brillo::Blob original_data = brillo::BlobFromString(test_data);
  brillo::Blob compressed_data, reconstructed_data;

  // Compress and confirm that compressed contents changed
  ASSERT_TRUE(util::Compress(original_data, &compressed_data));
  ASSERT_TRUE(compressed_data != original_data);

  // Decompress and confirm that contents match the original data
  ASSERT_TRUE(util::Decompress(compressed_data, &reconstructed_data));
  ASSERT_TRUE(reconstructed_data == original_data);
}
