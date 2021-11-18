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
  kPDPowerSource = 11,
  kPDSourcingDevice = 12,
  kNonPDPowerSource = 13,
  kPDSink = 14,
  kPDSinkingHost = 15,
  kNonPDSink = 16,
  kMaxValue = kNonPDSink,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class CableSpeedMetric {
  kOther = 0,
  kUSB2_0 = 1,
  kUSB3_2Gen1 = 2,
  kUSB3_2USB4Gen2 = 3,
  kUSB3_1Gen1 = 4,
  kUSB3_1Gen1Gen2 = 5,
  kUSB4Gen3 = 6,
  kTBTOnly10G20G = 7,
  kMaxValue = kTBTOnly10G20G,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class WrongConfigurationMetric {
  kTBTWrongCable = 0,
  kUSB4WrongCable = 1,
  kDPAltWrongCable = 2,
  kMaxValue = kDPAltWrongCable,
};

// A class for collecting UMA metrics.
class Metrics {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  ~Metrics() = default;

  void ReportPartnerType(PartnerTypeMetric type);
  void ReportCableSpeed(CableSpeedMetric speed);
  void ReportWrongCableError(WrongConfigurationMetric value);

 private:
  MetricsLibrary metrics_library_;
};

}  // namespace typecd

#endif  // TYPECD_METRICS_H_
