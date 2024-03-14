// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_
#define SHILL_CELLULAR_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_

#include <base/time/time.h>

#include "shill/callbacks.h"

namespace shill {
namespace mm1 {

// These are the methods that an org.freedesktop.ModemManager1.Modem.Location
// proxy must support. The interface is provided so that it can be mocked
// in tests. All calls are made asynchronously. Call completion is signalled
// via callbacks passed to the methods.
// Implemented as ChromeosModemLocationProxy.
class ModemLocationProxyInterface {
 public:
  virtual ~ModemLocationProxyInterface() = default;

  virtual void Setup(uint32_t sources,
                     bool signal_location,
                     ResultCallback callback,
                     base::TimeDelta timeout) = 0;

  virtual void GetLocation(BrilloAnyCallback callback,
                           base::TimeDelta timeout) = 0;
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MM1_MODEM_LOCATION_PROXY_INTERFACE_H_
