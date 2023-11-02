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

struct CompressionDecompressionFormatTestParams {
  ZlibCompressor::DeflateFormat deflate_format;
  ZlibDecompressor::InflateFormat inflate_format;
};

class CompressorFunctionalityTest
    : public testing::Test,
      public ::testing::WithParamInterface<
          CompressionDecompressionFormatTestParams> {};

class CompressorFormatTest : public CompressorFunctionalityTest {};

TEST_P(CompressorFunctionalityTest, CompressDecompressImmediateFlush) {
  const CompressionDecompressionFormatTestParams& test_param = GetParam();
  std::unique_ptr<ZlibCompressor> compressor =
      std::make_unique<ZlibCompressor>(test_param.deflate_format);
  std::unique_ptr<ZlibDecompressor> decompressor =
      std::make_unique<ZlibDecompressor>(test_param.inflate_format);
  EXPECT_TRUE(compressor->Initialize());
  EXPECT_TRUE(decompressor->Initialize());

  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor->Process(data_in, /*flush=*/true);
  ASSERT_TRUE(compressed);

  auto data_out = decompressor->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);

  EXPECT_EQ(data_in, data_out);
}

TEST_P(CompressorFunctionalityTest, CompressDecompressDelayedFlush) {
  const CompressionDecompressionFormatTestParams& test_param = GetParam();
  std::unique_ptr<ZlibCompressor> compressor =
      std::make_unique<ZlibCompressor>(test_param.deflate_format);
  std::unique_ptr<ZlibDecompressor> decompressor =
      std::make_unique<ZlibDecompressor>(test_param.inflate_format);
  EXPECT_TRUE(compressor->Initialize());
  EXPECT_TRUE(decompressor->Initialize());

  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor->Process(data_in, /*flush=*/false);
  ASSERT_TRUE(compressed);

  auto flushed = compressor->Process({}, /*flush=*/true);
  ASSERT_TRUE(flushed);
  compressed->insert(compressed->end(), flushed->begin(), flushed->end());

  auto data_out = decompressor->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);

  EXPECT_EQ(data_in, data_out);
}

TEST_P(CompressorFunctionalityTest, EmptyFlush) {
  const CompressionDecompressionFormatTestParams& test_param = GetParam();
  std::unique_ptr<ZlibCompressor> compressor =
      std::make_unique<ZlibCompressor>(test_param.deflate_format);
  EXPECT_TRUE(compressor->Initialize());

  auto flushed = compressor->Process({}, true);
  EXPECT_TRUE(flushed);
}

INSTANTIATE_TEST_SUITE_P(
    CompressorFunctionalityTestSuite,
    CompressorFunctionalityTest,
    testing::ValuesIn<CompressionDecompressionFormatTestParams>({
        {ZlibCompressor::DeflateFormat::Raw,
         ZlibDecompressor::InflateFormat::Raw},
        {ZlibCompressor::DeflateFormat::Zlib,
         ZlibDecompressor::InflateFormat::Zlib},
        {ZlibCompressor::DeflateFormat::Zlib,
         ZlibDecompressor::InflateFormat::ZlibOrGzip},
        {ZlibCompressor::DeflateFormat::Gzip,
         ZlibDecompressor::InflateFormat::Gzip},
        {ZlibCompressor::DeflateFormat::Gzip,
         ZlibDecompressor::InflateFormat::ZlibOrGzip},
    }));

TEST_P(CompressorFormatTest, CompressDecompressWrongFormat) {
  const CompressionDecompressionFormatTestParams& test_param = GetParam();
  std::unique_ptr<ZlibCompressor> compressor =
      std::make_unique<ZlibCompressor>(test_param.deflate_format);
  std::unique_ptr<ZlibDecompressor> decompressor =
      std::make_unique<ZlibDecompressor>(test_param.inflate_format);
  EXPECT_TRUE(compressor->Initialize());
  EXPECT_TRUE(decompressor->Initialize());

  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor->Process(data_in, /*flush=*/true);
  ASSERT_TRUE(compressed);

  auto data_out = decompressor->Process(compressed.value(), /*flush=*/true);
  EXPECT_FALSE(data_out);
}

INSTANTIATE_TEST_SUITE_P(
    CompressorDecomtTestSuite,
    CompressorFormatTest,
    testing::ValuesIn<CompressionDecompressionFormatTestParams>({
        {ZlibCompressor::DeflateFormat::Zlib,
         ZlibDecompressor::InflateFormat::Raw},
        {ZlibCompressor::DeflateFormat::Zlib,
         ZlibDecompressor::InflateFormat::Gzip},
        {ZlibCompressor::DeflateFormat::Raw,
         ZlibDecompressor::InflateFormat::Zlib},
        {ZlibCompressor::DeflateFormat::Raw,
         ZlibDecompressor::InflateFormat::Gzip},
        {ZlibCompressor::DeflateFormat::Raw,
         ZlibDecompressor::InflateFormat::ZlibOrGzip},
        {ZlibCompressor::DeflateFormat::Gzip,
         ZlibDecompressor::InflateFormat::Raw},
        {ZlibCompressor::DeflateFormat::Gzip,
         ZlibDecompressor::InflateFormat::Zlib},
    }));

// This test only works with raw inflate and deflate data since no headers and
// trailers are created. This allows clone_data to be the correct format for
// decompressing even when appending to it. With zlib or gzip compression,
// clone_data would be the concatenation of two compressed blocks that each have
// their own trailer and header, which can not be decompressed due to impromper
// formatting.
TEST(CompressorCloneTest, CompressDecompressClone) {
  std::unique_ptr<ZlibCompressor> compressor =
      std::make_unique<ZlibCompressor>(ZlibCompressor::DeflateFormat::Raw);
  std::unique_ptr<ZlibDecompressor> decompressor =
      std::make_unique<ZlibDecompressor>(ZlibDecompressor::InflateFormat::Raw);
  EXPECT_TRUE(compressor->Initialize());
  EXPECT_TRUE(decompressor->Initialize());

  std::vector<uint8_t> data_in(kUncompressedTestDataSize, 'x');

  auto compressed = compressor->Process(data_in, /*flush=*/false);
  ASSERT_TRUE(compressed);

  auto clone = compressor->Clone();
  ASSERT_TRUE(clone);

  // Process another data_in with the clone object and flush.
  auto clone_flushed = clone->Process(data_in, /*flush=*/true);
  ASSERT_TRUE(clone_flushed);
  std::vector<uint8_t> clone_data = compressed.value();
  clone_data.insert(clone_data.end(), clone_flushed->begin(),
                    clone_flushed->end());

  // Also flush the original object.
  auto flushed = compressor->Process({}, /*flush=*/true);
  ASSERT_TRUE(flushed);
  compressed->insert(compressed->end(), flushed->begin(), flushed->end());

  // Original data unchanged.
  auto data_out = decompressor->Process(compressed.value(), /*flush=*/true);
  ASSERT_TRUE(data_out);
  EXPECT_EQ(data_in, data_out);

  // Cloned one has processed data_in twice.
  auto clone_data_out = decompressor->Process(clone_data, /*flush=*/true);
  ASSERT_TRUE(clone_data_out);
  std::vector<uint8_t> expected_clone_data = data_in;
  expected_clone_data.insert(expected_clone_data.end(), data_in.begin(),
                             data_in.end());
  EXPECT_EQ(expected_clone_data, clone_data_out);
}

}  // namespace brillo
