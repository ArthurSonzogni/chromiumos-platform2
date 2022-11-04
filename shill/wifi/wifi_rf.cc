// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_rf.h"

#include <chromeos/dbus/shill/dbus-constants.h>

namespace shill {

std::string WiFiBandName(WiFiBand band) {
  switch (band) {
    case WiFiBand::kLowBand:
      return kBand2GHz;
    case WiFiBand::kHighBand:
      return kBand5GHz;
    case WiFiBand::kUltraHighBand:
      return kBand6GHz;
    case WiFiBand::kAllBands:
      return kBandAll;
    case WiFiBand::kUnknownBand:
    default:
      return kBandUnknown;
  }
}

WiFiBand WiFiBandFromName(const std::string& name) {
  if (name == kBand2GHz) {
    return WiFiBand::kLowBand;
  } else if (name == kBand5GHz) {
    return WiFiBand::kHighBand;
  } else if (name == kBand6GHz) {
    return WiFiBand::kUltraHighBand;
  } else if (name == kBandAll) {
    return WiFiBand::kAllBands;
  } else {
    return WiFiBand::kUnknownBand;
  }
}

}  // namespace shill
