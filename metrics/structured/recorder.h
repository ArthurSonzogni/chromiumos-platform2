// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_RECORDER_H_
#define METRICS_STRUCTURED_RECORDER_H_

namespace metrics::structured {

class EventBase;

// Base class for clients to interact with Structured metrics.
class Recorder {
 public:
  virtual ~Recorder() {}
  virtual bool Record(const EventBase& event) = 0;
  virtual void Flush() = 0;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_RECORDER_H_
