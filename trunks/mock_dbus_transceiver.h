// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MOCK_DBUS_TRANSCEIVER_H_
#define TRUNKS_MOCK_DBUS_TRANSCEIVER_H_

#include <string>

#include <base/functional/callback.h>
#include <gmock/gmock.h>

#include "trunks/command_transceiver.h"
#include "trunks/dbus_transceiver.h"
#include "trunks/mock_command_transceiver.h"

namespace trunks {

class MockDbusTransceiver : public DbusTransceiver,
                            public MockCommandTransceiver {
 public:
  MockDbusTransceiver();
  MockDbusTransceiver(const MockDbusTransceiver&) = delete;
  MockDbusTransceiver& operator=(const MockDbusTransceiver&) = delete;

  ~MockDbusTransceiver() override;

  MOCK_METHOD(void, StartEvent, (const std::string&), (override));
  MOCK_METHOD(void, StopEvent, (const std::string&), (override));
};

}  // namespace trunks

#endif  // TRUNKS_MOCK_DBUS_TRANSCEIVER_H_
