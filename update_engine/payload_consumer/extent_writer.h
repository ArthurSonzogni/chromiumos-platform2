// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_WRITER_H_

#include <memory>
#include <utility>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "update_engine/common/utils.h"
#include "update_engine/payload_consumer/file_descriptor.h"
#include "update_engine/update_metadata.pb.h"

// ExtentWriter is an abstract class which synchronously writes to a given
// file descriptor at the extents given.

namespace chromeos_update_engine {

class ExtentWriter {
 public:
  ExtentWriter() = default;
  virtual ~ExtentWriter() = default;

  // Returns true on success.
  virtual bool Init(FileDescriptorPtr fd,
                    const google::protobuf::RepeatedPtrField<Extent>& extents,
                    uint32_t block_size) = 0;

  // Returns true on success.
  virtual bool Write(const void* bytes, size_t count) = 0;
};

// DirectExtentWriter is probably the simplest ExtentWriter implementation.
// It writes the data directly into the extents.

class DirectExtentWriter : public ExtentWriter {
 public:
  DirectExtentWriter() = default;
  ~DirectExtentWriter() override = default;

  bool Init(FileDescriptorPtr fd,
            const google::protobuf::RepeatedPtrField<Extent>& extents,
            uint32_t block_size) override {
    fd_ = fd;
    block_size_ = block_size;
    extents_ = extents;
    cur_extent_ = extents_.begin();
    return true;
  }
  bool Write(const void* bytes, size_t count) override;

 private:
  FileDescriptorPtr fd_{nullptr};

  size_t block_size_{0};
  // Bytes written into |cur_extent_| thus far.
  uint64_t extent_bytes_written_{0};
  google::protobuf::RepeatedPtrField<Extent> extents_;
  // The next call to write should correspond to |cur_extents_|.
  google::protobuf::RepeatedPtrField<Extent>::iterator cur_extent_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_WRITER_H_
