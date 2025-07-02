// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_TRACE_READER_H_
#define FARFETCHD_TRACE_READER_H_

#include <string>

namespace farfetchd {

// Interface for reading trace data - allows for testing with mock
// implementations
class TraceReader {
 public:
  virtual ~TraceReader() = default;
  virtual bool Open() = 0;
  virtual bool ReadLine(std::string* line) = 0;
  virtual void Close() = 0;
};

}  // namespace farfetchd

#endif  // FARFETCHD_TRACE_READER_H_
