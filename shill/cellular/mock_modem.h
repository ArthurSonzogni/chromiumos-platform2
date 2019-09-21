// Copyright 2018 The Chromium OS Authors. All rights reserved.
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
  MockModem(const std::string& service,
            const RpcIdentifier& path,
            ModemInfo* modem_info);
  ~MockModem() override;

  // This class only mocks the pure virtual methods; if you need a
  // more thorough mock, know that modem_test.cc depends on the
  // incompleteness of this mock.
  MOCK_METHOD(bool,
              GetLinkName,
              (const KeyValueStore&, std::string*),
              (const, override));
  MOCK_METHOD(std::string, GetModemInterface, (), (const, override));
  MOCK_METHOD(Cellular*,
              ConstructCellular,
              (const std::string&, const std::string&, int),
              (override));
};

using StrictModem = ::testing::StrictMock<MockModem>;

}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MODEM_H_
