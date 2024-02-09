// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_FAKE_EXTENT_WRITER_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_FAKE_EXTENT_WRITER_H_

#include <memory>

#include <brillo/secure_blob.h>

#include "update_engine/payload_consumer/extent_writer.h"

namespace chromeos_update_engine {

// FakeExtentWriter is a concrete ExtentWriter subclass that keeps track of all
// the written data, useful for testing.
class FakeExtentWriter : public ExtentWriter {
 public:
  FakeExtentWriter() = default;
  FakeExtentWriter(const FakeExtentWriter&) = delete;
  FakeExtentWriter& operator=(const FakeExtentWriter&) = delete;

  ~FakeExtentWriter() override = default;

  // ExtentWriter overrides.
  bool Init(FileDescriptorPtr /* fd */,
            const google::protobuf::RepeatedPtrField<Extent>& /* extents */,
            uint32_t /* block_size */) override {
    init_called_ = true;
    return true;
  };
  bool Write(const void* bytes, size_t count) override {
    if (!init_called_)
      return false;
    written_data_.insert(written_data_.end(),
                         reinterpret_cast<const uint8_t*>(bytes),
                         reinterpret_cast<const uint8_t*>(bytes) + count);
    return true;
  }

  // Fake methods.
  bool InitCalled() { return init_called_; }
  brillo::Blob WrittenData() { return written_data_; }

 private:
  bool init_called_{false};
  brillo::Blob written_data_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_FAKE_EXTENT_WRITER_H_
