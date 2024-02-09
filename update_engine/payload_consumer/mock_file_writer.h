// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_MOCK_FILE_WRITER_H_
#define UPDATE_ENGINE_MOCK_FILE_WRITER_H_

#include <gmock/gmock.h>
#include "update_engine/payload_consumer/file_writer.h"

namespace chromeos_update_engine {

class MockFileWriter : public FileWriter {
 public:
  MOCK_METHOD2(Write, bool(const void* bytes, size_t count));
  MOCK_METHOD3(Write, bool(const void* bytes, size_t count, ErrorCode* error));
  MOCK_METHOD0(Close, int());
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_MOCK_FILE_WRITER_H_
