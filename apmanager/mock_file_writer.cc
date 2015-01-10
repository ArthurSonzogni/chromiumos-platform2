// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apmanager/mock_file_writer.h"

namespace apmanager {

namespace {
base::LazyInstance<MockFileWriter> g_mock_file_writer
    = LAZY_INSTANCE_INITIALIZER;
}  // namespace

MockFileWriter::MockFileWriter() {}
MockFileWriter::~MockFileWriter() {}

MockFileWriter* MockFileWriter::GetInstance() {
  return g_mock_file_writer.Pointer();
}

}  // namespace apmanager
