// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_METRICS_H_
#define TYPECD_METRICS_H_

#include <metrics/metrics_library.h>

namespace typecd {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class PartnerTypeMetric {
  kOther = 0,
  kTBTDPAltHub = 1,
  kTBTDPAltPeripheral = 2,
  kTBTHub = 3,
  kTBTPeripheral = 4,
  kUSB4Hub = 5,
  kUSB4Peripheral = 6,
  kDPAltHub = 7,
  kDPAltPeripheral = 8,
  kUSBHub = 9,
  kUSBPeripheral = 10,
  kMaxValue = kUSBPeripheral,
};

// A class for collecting UMA metrics.
class Metrics {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  ~Metrics() = default;

  void ReportPartnerType(PartnerTypeMetric type);

 private:
  MetricsLibrary metrics_library_;
};

}  // namespace typecd

#endif  // TYPECD_METRICS_H_
