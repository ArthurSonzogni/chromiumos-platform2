// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_MM1_MODEM_LOCATION_PROXY_H_
#define SHILL_MOCK_MM1_MODEM_LOCATION_PROXY_H_

#include <base/basictypes.h>
#include <gmock/gmock.h>

#include "shill/mm1_modem_location_proxy_interface.h"

namespace shill {
namespace mm1 {

class MockModemLocationProxy : public ModemLocationProxyInterface {
 public:
  MockModemLocationProxy();
  ~MockModemLocationProxy() override;

  // Inherited methods from ModemLocationProxyInterface.
  MOCK_METHOD5(Setup, void(uint32_t sources,
                           bool signal_location,
                           Error *error,
                           const ResultCallback &callback,
                           int timeout));

  MOCK_METHOD3(GetLocation, void(Error *error,
                                 const DBusEnumValueMapCallback &callback,
                                 int timeout));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockModemLocationProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_MOCK_MM1_MODEM_LOCATION_PROXY_H_
