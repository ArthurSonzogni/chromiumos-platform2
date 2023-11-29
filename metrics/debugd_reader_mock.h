// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_DEBUGD_READER_MOCK_H_
#define METRICS_DEBUGD_READER_MOCK_H_

#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "metrics/debugd_reader.h"

namespace chromeos_metrics {

class DebugdReaderMock : public DebugdReader {
 public:
  DebugdReaderMock(dbus::Bus* bus, std::string log_name)
      : DebugdReader(bus, log_name) {}

  MOCK_METHOD(std::optional<std::string>, Read, (), (override));
};

}  // namespace chromeos_metrics

#endif  // METRICS_DEBUGD_READER_MOCK_H_
