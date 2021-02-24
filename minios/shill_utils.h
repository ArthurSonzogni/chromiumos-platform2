// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SHILL_UTILS_H_
#define MINIOS_SHILL_UTILS_H_

#include <string>

namespace minios {

enum class WifiStationType {
  MANAGED,
  ADHOC,
};

std::string ToString(WifiStationType station);

enum class WifiSecurityType {
  NONE,
  PSK,
};

std::string ToString(WifiSecurityType security);

enum class WifiTechnologyType {
  WIFI,
  CELLULAR,
};

std::string ToString(WifiTechnologyType technology);

}  // namespace minios

#endif  // MINIOS_SHILL_UTILS_H_
