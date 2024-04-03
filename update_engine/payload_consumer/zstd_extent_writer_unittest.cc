// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/memory/ptr_util.h>
#include <gtest/gtest.h>

#include "update_engine/payload_consumer/fake_extent_writer.h"
#include "update_engine/payload_consumer/zstd_extent_writer.h"

namespace chromeos_update_engine {

namespace {

const char kData[] = "c4173e45-4989-4a7b-b3cf-d5e0eee62373";

// zstd compression for `kData`.
const uint8_t kCompressedData[] = {
    0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x88, 0x21, 0x01, 0x00, 0x63,
    0x34, 0x31, 0x37, 0x33, 0x65, 0x34, 0x35, 0x2d, 0x34, 0x39,
    0x38, 0x39, 0x2d, 0x34, 0x61, 0x37, 0x62, 0x2d, 0x62, 0x33,
    0x63, 0x66, 0x2d, 0x64, 0x35, 0x65, 0x30, 0x65, 0x65, 0x65,
    0x36, 0x32, 0x33, 0x37, 0x33, 0xd8, 0x4f, 0x91, 0x1f,
};

// zstd compression for 36KiB of 'a'.
const uint8_t kCompressedA36KData[]{
    0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x88, 0x4d, 0x00, 0x00, 0x08, 0x61,
    0x01, 0x00, 0xfc, 0x0f, 0x1d, 0x08, 0x01, 0x35, 0x4b, 0x42, 0xca,
};

}  // namespace

class ZstdExtentWriterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_extent_writer_ = new FakeExtentWriter();
    zstd_writer_.reset(
        new ZstdExtentWriter(base::WrapUnique(fake_extent_writer_)));
  }

  void WriteAll(const brillo::Blob& compressed) {
    ASSERT_TRUE(zstd_writer_->Init(fd_, {}, 1024));
    EXPECT_TRUE(zstd_writer_->Write(compressed.data(), compressed.size()));

    EXPECT_TRUE(fake_extent_writer_->InitCalled());
  }

  FakeExtentWriter* fake_extent_writer_{nullptr};
  std::unique_ptr<ZstdExtentWriter> zstd_writer_;

  const brillo::Blob data_blob_{std::begin(kData),
                                std::begin(kData) + strlen(kData)};
  FileDescriptorPtr fd_;
};

TEST_F(ZstdExtentWriterTest, CreateAndDestroy) {
  EXPECT_FALSE(fake_extent_writer_->InitCalled());
}

TEST_F(ZstdExtentWriterTest, CompressedData) {
  WriteAll(
      brillo::Blob(std::begin(kCompressedData), std::end(kCompressedData)));
  EXPECT_EQ(data_blob_, fake_extent_writer_->WrittenData());
}

TEST_F(ZstdExtentWriterTest, CompressedDataBiggerThanTheBuffer) {
  // Test that even if the output data is bigger than the internal buffer, all
  // the data is written.
  WriteAll(brillo::Blob(std::begin(kCompressedA36KData),
                        std::end(kCompressedA36KData)));
  EXPECT_EQ(brillo::Blob(36 * 1024, 'a'), fake_extent_writer_->WrittenData());
}

TEST_F(ZstdExtentWriterTest, GarbageDataRejected) {
  ASSERT_TRUE(zstd_writer_->Init(fd_, {}, 1024));
  EXPECT_FALSE(zstd_writer_->Write(data_blob_.data(), data_blob_.size()));
}

TEST_F(ZstdExtentWriterTest, PartialDataStreamingIn) {
  brillo::Blob compressed(std::begin(kCompressedA36KData),
                          std::end(kCompressedA36KData));
  ASSERT_TRUE(zstd_writer_->Init(fd_, {}, 1024));
  for (uint8_t byte : compressed) {
    EXPECT_TRUE(zstd_writer_->Write(&byte, 1));
  }

  EXPECT_EQ(brillo::Blob(36 * 1024, 'a'), fake_extent_writer_->WrittenData());
}

}  // namespace chromeos_update_engine
