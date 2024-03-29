// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_READER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_READER_H_

#include <vector>

#include "update_engine/payload_consumer/file_descriptor.h"
#include "update_engine/update_metadata.pb.h"

namespace chromeos_update_engine {

// ExtentReader is an abstract class with reads from a given file descriptor at
// the extents given.
class ExtentReader {
 public:
  virtual ~ExtentReader() = default;

  // Initializes |ExtentReader|
  virtual bool Init(FileDescriptorPtr fd,
                    const google::protobuf::RepeatedPtrField<Extent>& extents,
                    uint32_t block_size) = 0;

  // Seeks to the given |offset| assuming all extents are concatenated together.
  virtual bool Seek(uint64_t offset) = 0;

  // Returns true on success.
  virtual bool Read(void* buffer, size_t count) = 0;
};

// DirectExtentReader is probably the simplest ExtentReader implementation.
// It reads the data directly from the extents.
class DirectExtentReader : public ExtentReader {
 public:
  DirectExtentReader() = default;
  DirectExtentReader(const DirectExtentReader&) = delete;
  DirectExtentReader& operator=(const DirectExtentReader&) = delete;

  ~DirectExtentReader() override = default;

  bool Init(FileDescriptorPtr fd,
            const google::protobuf::RepeatedPtrField<Extent>& extents,
            uint32_t block_size) override;
  bool Seek(uint64_t offset) override;
  bool Read(void* bytes, size_t count) override;

 private:
  FileDescriptorPtr fd_{nullptr};
  google::protobuf::RepeatedPtrField<Extent> extents_;
  size_t block_size_{0};

  // Current extent being read from |fd_|.
  google::protobuf::RepeatedPtrField<Extent>::iterator cur_extent_;

  // Bytes read from |cur_extent_| thus far.
  uint64_t cur_extent_bytes_read_{0};

  // Offset assuming all extents are concatenated.
  uint64_t offset_{0};

  // The accelaring upper bounds for |extents_| if we assume all extents are
  // concatenated.
  std::vector<uint64_t> extents_upper_bounds_;
  uint64_t total_size_{0};
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_EXTENT_READER_H_
