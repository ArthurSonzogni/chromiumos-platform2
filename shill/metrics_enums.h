// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_ENUMS_H_
#define SHILL_METRICS_ENUMS_H_

namespace shill {

// A static class defined only as a temporary place to host Metrics's enums
// in a header file that can be included anywhere in shill without creating
// circular dependencies. This file allows to remove all dependencies of Metrics
// onto internal shill classes and migrate enum conversion functions to allow
// using Metrics more easily across shill.
// TODO(b/268579868): Fold back these enums into metrics.h once b/268579868 is
// resolved.
class MetricsEnums {
 public:
  virtual ~MetricsEnums();

  enum WirelessSecurity {
    kWirelessSecurityUnknown = 0,
    kWirelessSecurityNone = 1,
    kWirelessSecurityWep = 2,
    kWirelessSecurityWpa = 3,
    // Value "802.11i/RSN" (4) is not used anymore.
    kWirelessSecurity8021x = 5,
    kWirelessSecurityPsk = 6,
    kWirelessSecurityWpa3 = 7,
    kWirelessSecurityWpaWpa2 = 8,
    kWirelessSecurityWpa2 = 9,
    kWirelessSecurityWpa2Wpa3 = 10,
    kWirelessSecurityWpaEnterprise = 11,
    kWirelessSecurityWpaWpa2Enterprise = 12,
    kWirelessSecurityWpa2Enterprise = 13,
    kWirelessSecurityWpa2Wpa3Enterprise = 14,
    kWirelessSecurityWpa3Enterprise = 15,
    kWirelessSecurityWpaAll = 16,
    kWirelessSecurityWpaAllEnterprise = 17,
    kWirelessSecurityWepEnterprise = 18,

    kWirelessSecurityMax
  };

 protected:
  MetricsEnums();
  MetricsEnums(const MetricsEnums&) = delete;
  MetricsEnums& operator=(const MetricsEnums&) = delete;
};

}  // namespace shill

#endif  // SHILL_METRICS_ENUMS_H_
