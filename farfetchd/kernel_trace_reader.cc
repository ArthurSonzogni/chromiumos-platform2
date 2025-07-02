// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "farfetchd/kernel_trace_reader.h"

#include <fstream>
#include <memory>

namespace farfetchd {

KernelTraceReader::KernelTraceReader() = default;

KernelTraceReader::~KernelTraceReader() = default;

bool KernelTraceReader::Open() {
  trace_pipe_ =
      std::make_unique<std::ifstream>("/sys/kernel/debug/tracing/trace_pipe");
  return trace_pipe_->is_open();
}

bool KernelTraceReader::ReadLine(std::string* line) {
  if (!trace_pipe_ || !trace_pipe_->is_open()) {
    return false;
  }
  return static_cast<bool>(std::getline(*trace_pipe_, *line));
}

void KernelTraceReader::Close() {
  if (trace_pipe_) {
    trace_pipe_->close();
    trace_pipe_.reset();
  }
}

}  // namespace farfetchd
