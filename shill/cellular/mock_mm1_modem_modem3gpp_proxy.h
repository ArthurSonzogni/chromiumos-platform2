// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_MM1_MODEM_MODEM3GPP_PROXY_H_
#define SHILL_CELLULAR_MOCK_MM1_MODEM_MODEM3GPP_PROXY_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/cellular/mm1_modem_modem3gpp_proxy_interface.h"

namespace shill {
namespace mm1 {

class MockModemModem3gppProxy : public ModemModem3gppProxyInterface {
 public:
  MockModemModem3gppProxy();
  ~MockModemModem3gppProxy() override;

  MOCK_METHOD(void,
              Register,
              (const std::string&, Error*, const ResultCallback&, int),
              (override));
  MOCK_METHOD(void,
              Scan,
              (Error*, const KeyValueStoresCallback&, int),
              (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockModemModem3gppProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MM1_MODEM_MODEM3GPP_PROXY_H_
