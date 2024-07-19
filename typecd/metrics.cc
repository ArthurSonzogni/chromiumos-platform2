// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/metrics.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <metrics/structured_events.h>

namespace {
constexpr char kPartnerTypeMetricName[] = "ChromeOS.TypeC.PartnerType";
constexpr char kCableSpeedMetricName[] = "ChromeOS.TypeC.CableSpeed";
constexpr char kWrongConfigurationMetricName[] =
    "ChromeOS.TypeC.WrongConfiguration";
constexpr char kPartnerLocationMetricName[] = "ChromeOS.TypeC.PartnerLocation";
constexpr char kPowerSourceLocationMetricName[] =
    "ChromeOS.TypeC.PowerSourceLocation";
constexpr char kDpSuccessMetricName[] = "ChromeOS.TypeC.DpSuccess";
constexpr char kModeEntryMetricName[] = "ChromeOS.TypeC.ModeEntry";
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

void Metrics::ReportDpSuccess(DpSuccessMetric val) {
  if (!metrics_library_.SendEnumToUMA(kDpSuccessMetricName, val)) {
    LOG(WARNING) << "Failed to send DP success sample to UMA, val: "
                 << static_cast<int>(val);
  }
}

void Metrics::ReportModeEntry(ModeEntryMetric val) {
  if (!metrics_library_.SendEnumToUMA(kModeEntryMetricName, val)) {
    LOG(WARNING) << "Failed to send Mode Entry sample to UMA, val: "
                 << static_cast<int>(val);
  }
}

void Metrics::ReportBasicPdDeviceInfo(int vid,
                                      int pid,
                                      int xid,
                                      bool supports_pd,
                                      bool supports_usb,
                                      bool supports_dp,
                                      bool supports_tbt,
                                      bool supports_usb4,
                                      DataRoleMetric data_role,
                                      PowerRoleMetric power_role,
                                      PartnerTypeMetric type) {
  metrics::structured::events::usb_pd_device::UsbPdDeviceInfo()
      .SetVendorId(vid)
      .SetProductId(pid)
      .SetExitId(xid)
      .SetSupportsPd(supports_pd)
      .SetSupportsUsb(supports_usb)
      .SetSupportsDp(supports_dp)
      .SetSupportsTbt(supports_tbt)
      .SetSupportsUsb4(supports_usb4)
      .SetDataRole(static_cast<int>(data_role))
      .SetPowerRole(static_cast<int>(power_role))
      .SetPartnerType(static_cast<int>(type))
      .Record();
}

void Metrics::ReportPdConnect(std::string boot_id,
                              std::string usb2_id,
                              std::string usb3_id,
                              int vid,
                              int pid,
                              PartnerTypeMetric partner_type,
                              CableSpeedMetric cable_speed,
                              ModeEntryMetric mode_entry) {
  // TODO(b/354255393): Add support for max charging rate and realized
  // charging rate in typecd structured metrics.
  metrics::structured::events::usb_quality::UsbPdConnect()
      .SetBootId(std::move(boot_id))
      .SetUsb2ConnectionId(std::move(usb2_id))
      .SetUsb3ConnectionId(std::move(usb3_id))
      .SetVendorId(vid)
      .SetProductId(pid)
      .SetPartnerType(static_cast<int>(partner_type))
      .SetCableType(static_cast<int>(cable_speed))
      .SetMaxChargingRate(0)
      .SetRealizedChargingRate(0)
      .SetModeEntryResult(static_cast<int>(mode_entry))
      .Record();
}

}  // namespace typecd
