// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_MOCK_RECORDER_H_
#define METRICS_STRUCTURED_MOCK_RECORDER_H_

#include "metrics/structured/recorder.h"

#include <gmock/gmock.h>

namespace metrics::structured {

class MockRecorder : public Recorder {
 public:
  MOCK_METHOD(bool, Record, (const EventBase&), (override));
  MOCK_METHOD(void, Flush, (), (override));
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_MOCK_RECORDER_H_
