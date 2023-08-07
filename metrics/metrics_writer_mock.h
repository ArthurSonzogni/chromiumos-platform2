// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_METRICS_WRITER_MOCK_H_
#define METRICS_METRICS_WRITER_MOCK_H_

#include "metrics/metrics_writer.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>

class MetricsWriterMock : public MetricsWriter {
 public:
  MOCK_METHOD(bool,
              WriteMetrics,
              (std::vector<metrics::MetricSample>),
              (override));
  // Change the output file path.
  MOCK_METHOD(bool, SetOutputFile, (const std::string&), (override));
};

#endif  // METRICS_METRICS_WRITER_MOCK_H_
