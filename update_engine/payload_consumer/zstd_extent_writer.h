// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_ZSTD_EXTENT_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_ZSTD_EXTENT_WRITER_H_

#include <zstd.h>

#include <memory>
#include <utility>

#include <brillo/secure_blob.h>

#include "update_engine/payload_consumer/extent_writer.h"

// ZstdExtentWriter is a concrete ExtentWriter subclass.
// Performs zstandard decompressions.

namespace chromeos_update_engine {

class ZstdExtentWriter : public ExtentWriter {
 public:
  explicit ZstdExtentWriter(std::unique_ptr<ExtentWriter> writer)
      : writer_(std::move(writer)) {}
  ~ZstdExtentWriter() override;

  bool Init(FileDescriptorPtr fd,
            const google::protobuf::RepeatedPtrField<Extent>& extents,
            uint32_t block_size) override;
  bool Write(const void* bytes, size_t count) override;

 private:
  std::unique_ptr<ExtentWriter> writer_;  // The underlying ExtentWriter.
  ZSTD_DStream* d_stream_ = nullptr;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_ZSTD_EXTENT_WRITER_H_
