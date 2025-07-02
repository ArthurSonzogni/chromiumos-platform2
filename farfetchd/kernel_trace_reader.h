// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_KERNEL_TRACE_READER_H_
#define FARFETCHD_KERNEL_TRACE_READER_H_

#include <fstream>
#include <memory>
#include <string>

#include "farfetchd/trace_reader.h"

namespace farfetchd {

// Real implementation that reads from kernel tracefs
class KernelTraceReader : public TraceReader {
 public:
  KernelTraceReader();
  ~KernelTraceReader() override;

  bool Open() override;
  bool ReadLine(std::string* line) override;
  void Close() override;

 private:
  std::unique_ptr<std::ifstream> trace_pipe_;
};

}  // namespace farfetchd

#endif  // FARFETCHD_KERNEL_TRACE_READER_H_
