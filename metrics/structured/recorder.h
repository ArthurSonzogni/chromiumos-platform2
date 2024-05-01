// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_RECORDER_H_
#define METRICS_STRUCTURED_RECORDER_H_

#include <base/time/time.h>

namespace metrics::structured {

class EventBase;

// Base class for clients to interact with Structured metrics.
class Recorder {
 public:
  // Parameters for constructing different recorders for clients to use.
  //
  // A flush occurs if the last write occurred |write_cadence| ago OR if the
  // events in-memory exceed |max_in_memory_size_kb|.
  struct RecorderParams {
    // Time elapsed since the last write before a flush has occurred.
    base::TimeDelta write_cadence;

    // Max in-memory size in bytes before a flush triggers.
    int max_in_memory_size_bytes = 0;
  };

  virtual ~Recorder() {}
  virtual bool Record(const EventBase& event) = 0;
  virtual void Flush() = 0;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_RECORDER_H_
