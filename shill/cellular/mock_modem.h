// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_MODEM_H_
#define SHILL_CELLULAR_MOCK_MODEM_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/cellular/modem.h"

namespace shill {

class MockModem : public Modem {
 public:
  MockModem(const std::string& owner,
            const std::string& service,
            const std::string& path,
            ModemInfo* modem_info,
            ControlInterface* control_interface);
  ~MockModem() override;

  // This class only mocks the pure virtual methods; if you need a
  // more thorough mock, know that modem_unittest.cc depends on the
  // incompleteness of this mock.
  MOCK_METHOD1(SetModemStateFromProperties,
               void(const DBusPropertiesMap& properties));
  MOCK_CONST_METHOD2(GetLinkName,
                     bool(const DBusPropertiesMap& modem_properties,
                          std::string* name));
  MOCK_CONST_METHOD0(GetModemInterface,
                     std::string(void));
  MOCK_METHOD3(ConstructCellular, Cellular*(
      const std::string& link_name,
      const std::string& device_name,
      int ifindex));
};
typedef ::testing::StrictMock<MockModem> StrictModem;

}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MODEM_H_
