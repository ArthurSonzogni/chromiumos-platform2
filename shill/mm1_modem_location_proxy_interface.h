// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_
#define SHILL_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_

#include "shill/callbacks.h"

namespace shill {
class Error;

namespace mm1 {

// These are the methods that an org.freedesktop.ModemManager1.Modem.Location
// proxy must support. The interface is provided so that it can be mocked
// in tests. All calls are made asynchronously. Call completion is signalled
// via callbacks passed to the methods.
class ModemLocationProxyInterface {
 public:
  virtual ~ModemLocationProxyInterface() {}

  virtual void Setup(uint32_t sources,
                     bool signal_location,
                     Error *error,
                     const ResultCallback &callback,
                     int timeout) = 0;

  virtual void GetLocation(Error *error,
                           const DBusEnumValueMapCallback &callback,
                           int timeout) = 0;
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_
