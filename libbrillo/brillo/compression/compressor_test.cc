// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "brillo/compression/compressor_interface.h"
#include "brillo/compression/zlib_compressor.h"

namespace brillo {

namespace {
// 10000 is an arbitrarily chosen value to test that the data size is conserved
// after compression and decompression.
constexpr size_t kUncompressedTestDataSize = 10000;
}  // namespace

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
  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/true);
  ASSERT_TRUE(compressed);

  auto data_out = decompressor_->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);

  EXPECT_EQ(data_in, data_out);
}

TEST_F(CompressorTest, CompressDecompressNoFlush) {
  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/false);
  ASSERT_TRUE(compressed);

  auto flushed = compressor_->Process({}, /*flush=*/true);
  ASSERT_TRUE(flushed);
  compressed->insert(compressed->end(), flushed->begin(), flushed->end());

  auto data_out = decompressor_->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);

  EXPECT_EQ(data_in, data_out);
}

TEST_F(CompressorTest, CompressDecompressClone) {
  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor_->Process(data_in, /*flush=*/false);
  ASSERT_TRUE(compressed);

  auto clone = compressor_->Clone();
  ASSERT_TRUE(clone);

  // Process another data_in with the clone object and flush.
  auto clone_flushed = clone->Process(data_in, /*flush=*/true);
  ASSERT_TRUE(clone_flushed);
  std::vector<uint8_t> clone_data = compressed.value();
  clone_data.insert(clone_data.end(), clone_flushed->begin(),
                    clone_flushed->end());

  // Also flush the original object.
  auto flushed = compressor_->Process({}, /*flush=*/true);
  ASSERT_TRUE(flushed);
  compressed->insert(compressed->end(), flushed->begin(), flushed->end());

  // Original data unchanged.
  auto data_out = decompressor_->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);
  EXPECT_EQ(data_in, data_out);

  // Cloned one has processed data_in twice.
  auto clone_data_out = decompressor_->Process(clone_data, /*flush=*/true);
  ASSERT_TRUE(clone_data_out);
  std::vector<uint8_t> expected_clone_data = data_in;
  expected_clone_data.insert(expected_clone_data.end(), data_in.begin(),
                             data_in.end());
  EXPECT_EQ(expected_clone_data, clone_data_out);
}

TEST_F(CompressorTest, EmptyFlush) {
  auto flushed = compressor_->Process({}, true);
  EXPECT_TRUE(flushed);
}

}  // namespace brillo
