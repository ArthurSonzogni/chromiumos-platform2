// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEBUGD_PROXY_INTERFACE_H_
#define SHILL_DEBUGD_PROXY_INTERFACE_H_

#include <chromeos/dbus/debugd/dbus-constants.h>

namespace shill {

class DebugdProxyInterface {
 public:
  virtual ~DebugdProxyInterface() = default;

  // Generate firmware dump asynchronously for a specific firmware dump type,
  // e.g. WiFi. More usage information can be found at the org.chromium.debugd
  // D-Bus API doc.
  virtual void GenerateFirmwareDump(const debugd::FirmwareDumpType& type) = 0;
};

}  // namespace shill

#endif  // SHILL_DEBUGD_PROXY_INTERFACE_H_
