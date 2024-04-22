// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_TECHNOLOGY_H_
#define NET_BASE_TECHNOLOGY_H_

#include <optional>
#include <string_view>

#include <brillo/brillo_export.h>

namespace net_base {

// The enum representing a link transport technology type.
enum class BRILLO_EXPORT Technology {
  kCellular,
  kEthernet,
  kVPN,
  kWiFi,
  kWiFiDirect,
};

// Converts between the technology and the string.
BRILLO_EXPORT std::string_view ToString(Technology);
BRILLO_EXPORT std::optional<Technology> FromString(std::string_view str);

// Add the Technology to the ostream.
BRILLO_EXPORT std::ostream& operator<<(std::ostream& os, Technology technology);

}  // namespace net_base

#endif  // NET_BASE_TECHNOLOGY_H_
