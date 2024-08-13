// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_CELLULAR_CONSTS_H_
#define SHILL_CELLULAR_CELLULAR_CONSTS_H_

#include "shill/device_id.h"

namespace cellular {

// APN info properties added in runtime.
// Property added in shill to the last good APN to be able to reset/obsolete
// it by changing the version.
const char kApnVersionProperty[] = "version";
const int kCurrentApnCacheVersion = 2;

// APN Source.
const char kApnSourceFallback[] = "fallback";

// Modem identifiers
const int kL850GLVid = 0x2cb7;
const int kL850GLPid = 0x0007;
const shill::DeviceId::BusType kL850GLBusType = shill::DeviceId::BusType::kUsb;

const int kFM101Vid = 0x2cb7;
const int kFM101Pid = 0x01a2;
const shill::DeviceId::BusType kFM101BusType = shill::DeviceId::BusType::kUsb;

const int kRW101Vid = 0x33f8;
const int kRW101Pid = 0x01a2;
const shill::DeviceId::BusType kRW101BusType = shill::DeviceId::BusType::kUsb;

const int kRW135Vid = 0x33f8;
const int kRW135Pid = 0x0115;
const shill::DeviceId::BusType kRW135BusType = shill::DeviceId::BusType::kUsb;

const int kFM350Vid = 0x14c3;
const int kFM350Pid = 0x4d75;
const shill::DeviceId::BusType kFM350BusType = shill::DeviceId::BusType::kPci;

}  // namespace cellular

#endif  // SHILL_CELLULAR_CELLULAR_CONSTS_H_
