// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_BZIP_EXTENT_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_BZIP_EXTENT_WRITER_H_

#include <bzlib.h>
#include <memory>
#include <utility>

#include <brillo/secure_blob.h>

#include "update_engine/common/utils.h"
#include "update_engine/payload_consumer/extent_writer.h"

// BzipExtentWriter is a concrete ExtentWriter subclass that bzip-decompresses
// what it's given in Write. It passes the decompressed data to an underlying
// ExtentWriter.

namespace chromeos_update_engine {

class BzipExtentWriter : public ExtentWriter {
 public:
  explicit BzipExtentWriter(std::unique_ptr<ExtentWriter> next)
      : next_(std::move(next)) {
    memset(&stream_, 0, sizeof(stream_));
  }
  ~BzipExtentWriter() override;

  bool Init(FileDescriptorPtr fd,
            const google::protobuf::RepeatedPtrField<Extent>& extents,
            uint32_t block_size) override;
  bool Write(const void* bytes, size_t count) override;

 private:
  std::unique_ptr<ExtentWriter> next_;  // The underlying ExtentWriter.
  bz_stream stream_;                    // the libbz2 stream
  brillo::Blob input_buffer_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_BZIP_EXTENT_WRITER_H_
