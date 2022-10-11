// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/metrics.h"

#include <base/logging.h>

namespace {
constexpr char kPartnerTypeMetricName[] = "ChromeOS.TypeC.PartnerType";
constexpr char kCableSpeedMetricName[] = "ChromeOS.TypeC.CableSpeed";
constexpr char kWrongConfigurationMetricName[] =
    "ChromeOS.TypeC.WrongConfiguration";
constexpr char kPartnerLocationMetricName[] = "ChromeOS.TypeC.PartnerLocation";
constexpr char kPowerSourceLocationMetricName[] =
    "ChromeOS.TypeC.PowerSourceLocation";
}  // namespace

namespace typecd {

void Metrics::ReportPartnerType(PartnerTypeMetric type) {
  if (!metrics_library_.SendEnumToUMA(kPartnerTypeMetricName, type)) {
    LOG(WARNING) << "Failed to send partner type sample to UMA, type: "
                 << static_cast<int>(type);
  }
}

void Metrics::ReportCableSpeed(CableSpeedMetric speed) {
  if (!metrics_library_.SendEnumToUMA(kCableSpeedMetricName, speed)) {
    LOG(WARNING) << "Failed to send cable speed sample to UMA, speed: "
                 << static_cast<int>(speed);
  }
}

void Metrics::ReportWrongCableError(WrongConfigurationMetric value) {
  if (!metrics_library_.SendEnumToUMA(kWrongConfigurationMetricName, value)) {
    LOG(WARNING) << "Failed to send wrong cable config sample to UMA, value: "
                 << static_cast<int>(value);
  }
}

void Metrics::ReportPartnerLocation(PartnerLocationMetric location) {
  if (!metrics_library_.SendEnumToUMA(kPartnerLocationMetricName, location)) {
    LOG(WARNING) << "Failed to send partner location sample to UMA, location: "
                 << static_cast<int>(location);
  }
}

void Metrics::ReportPowerSourceLocation(PowerSourceLocationMetric location) {
  if (!metrics_library_.SendEnumToUMA(kPowerSourceLocationMetricName,
                                      location)) {
    LOG(WARNING)
        << "Failed to send power source location sample to UMA, location: "
        << static_cast<int>(location);
  }
}

}  // namespace typecd
