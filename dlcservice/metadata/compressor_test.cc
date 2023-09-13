// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "dlcservice/metadata/compressor_interface.h"
#include "dlcservice/metadata/metadata.h"
#include "dlcservice/metadata/zlib_compressor.h"

namespace dlcservice::metadata {

class CompressorTest : public testing::Test {
 public:
  void SetUp() override {
    testing::Test::SetUp();
    compressor_ = std::make_unique<ZlibCompressor>();
    decompressor_ = std::make_unique<ZlibDecompressor>();
    EXPECT_TRUE(compressor_->Initialize());
    EXPECT_TRUE(decompressor_->Initialize());
  }

 protected:
  std::unique_ptr<CompressorInterface> compressor_;
  std::unique_ptr<CompressorInterface> decompressor_;
};

TEST_F(CompressorTest, CompressDecompressFlush) {
  std::string data_in(kMaxMetadataFileSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/true);
  EXPECT_TRUE(compressed);

  auto data_out = decompressor_->Process(*compressed, /*flush=*/true);
  EXPECT_TRUE(data_out);

  EXPECT_EQ(data_in, *data_out);
}

TEST_F(CompressorTest, CompressDecompressNoFlush) {
  std::string data_in(kMaxMetadataFileSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/false);
  EXPECT_TRUE(compressed);

  auto flushed = compressor_->Process("", /*flush=*/true);
  EXPECT_TRUE(flushed);
  compressed->append(*flushed);

  auto data_out = decompressor_->Process(*compressed, /*flush=*/true);
  EXPECT_TRUE(data_out);

  EXPECT_EQ(data_in, *data_out);
}

TEST_F(CompressorTest, CompressDecompressClone) {
  std::string data_in(kMaxMetadataFileSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/false);
  EXPECT_TRUE(compressed);

  auto clone = compressor_->Clone();
  std::string clone_data = *compressed;
  EXPECT_TRUE(clone);

  // Process another data_in with the clone object and flush.
  auto clone_flushed = clone->Process(data_in, /*flush=*/true);
  EXPECT_TRUE(clone_flushed);
  clone_data.append(*clone_flushed);

  // Also flush the original object.
  auto flushed = compressor_->Process("", /*flush=*/true);
  EXPECT_TRUE(flushed);
  compressed->append(*flushed);

  // Original data unchanged.
  auto data_out = decompressor_->Process(*compressed, /*flush=*/true);
  EXPECT_TRUE(data_out);
  EXPECT_EQ(data_in, *data_out);

  // Cloned one has processed data_in twice.
  auto clone_data_out = decompressor_->Process(clone_data, /*flush=*/true);
  EXPECT_TRUE(clone_data_out);
  EXPECT_EQ(data_in + data_in, *clone_data_out);
}

TEST_F(CompressorTest, EmptyFlush) {
  auto flushed = compressor_->Process("", true);
  EXPECT_TRUE(flushed);
}

}  // namespace dlcservice::metadata
