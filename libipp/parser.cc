// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include "errors.h"
#include "frame.h"
#include "ipp_frame.h"
#include "ipp_parser.h"

namespace ipp {

void SimpleParserLog::AddParserError(const AttrPath& path,
                                     ParserCode error,
                                     bool critical) {
  if (errors_.size() < max_entries_count_) {
    errors_.emplace_back(path, error);
  }
  if (critical) {
    critical_errors_.emplace_back(path, error);
  }
}

Frame Parse(const uint8_t* buffer, size_t size, ParserLog& log) {
  Frame frame;
  if (buffer == nullptr) {
    size = 0;
  }
  std::vector<Log> log_temp;
  FrameData frame_data;
  Parser parser(&frame_data, &log_temp, log);
  parser.ReadFrameFromBuffer(buffer, buffer + size);
  parser.SaveFrameToPackage(false, &frame);
  frame.VersionNumber() = static_cast<Version>(frame_data.version_);
  frame.OperationIdOrStatusCode() = frame_data.operation_id_or_status_code_;
  frame.RequestId() = frame_data.request_id_;
  return frame;
}

}  // namespace ipp
