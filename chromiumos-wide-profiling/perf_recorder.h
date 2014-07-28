// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERF_RECORDER_H_
#define PERF_RECORDER_H_

#include <string>

#include "base/basictypes.h"

#include "perf_reader.h"
#include "quipper_proto.h"
#include "quipper_string.h"

namespace quipper {

class PerfRecorder {
 public:
  PerfRecorder() {}
  bool RecordAndConvertToProtobuf(const string& perf_command,
                                  const int time,
                                  quipper::PerfDataProto* perf_data);
 private:
  string GetSleepCommand(const int time);
  DISALLOW_COPY_AND_ASSIGN(PerfRecorder);
};

}  // namespace quipper

#endif  // PERF_RECORDER_H_
