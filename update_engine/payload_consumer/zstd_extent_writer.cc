// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/zstd_extent_writer.h"

namespace chromeos_update_engine {

ZstdExtentWriter::~ZstdExtentWriter() {
  TEST_AND_RETURN(!ZSTD_isError(ZSTD_freeDStream(d_stream_)));
}

bool ZstdExtentWriter::Init(
    FileDescriptorPtr fd,
    const google::protobuf::RepeatedPtrField<Extent>& extents,
    uint32_t block_size) {
  d_stream_ = ZSTD_createDStream();
  size_t init_d_stream = ZSTD_initDStream(d_stream_);

  if (ZSTD_isError(init_d_stream)) {
    LOG(ERROR) << "ZSTD initDStream failure: "
               << ZSTD_getErrorName(init_d_stream);
    return false;
  }

  if (size_t set_d_stream_windowlog = ZSTD_DCtx_setParameter(
          d_stream_, ZSTD_d_windowLogMax, ZSTD_WINDOWLOG_MAX_32);
      ZSTD_isError(set_d_stream_windowlog)) {
    LOG(ERROR) << "ZSTD set parameter failure: "
               << ZSTD_getErrorName(set_d_stream_windowlog);
    ZSTD_freeDCtx(d_stream_);
    d_stream_ = nullptr;
    return false;
  }

  return writer_->Init(fd, extents, block_size);
}

bool ZstdExtentWriter::Write(const void* bytes, size_t count) {
  ZSTD_inBuffer in_buf{
      .src = bytes,
      .size = count,
      .pos = 0,
  };

  brillo::Blob output_buffer(16 * 1024);
  while (in_buf.pos < in_buf.size) {
    ZSTD_outBuffer out_buf{
        .dst = output_buffer.data(),
        .size = output_buffer.size(),
        .pos = 0,
    };

    size_t decompress_stream =
        ZSTD_decompressStream(d_stream_, &out_buf, &in_buf);
    if (ZSTD_isError(decompress_stream)) {
      LOG(ERROR) << "ZSTD dcompressStream failure: "
                 << ZSTD_getErrorName(decompress_stream);
      return false;
    }

    TEST_AND_RETURN_FALSE(writer_->Write(output_buffer.data(), out_buf.pos));
  }

  return true;
}

}  // namespace chromeos_update_engine
