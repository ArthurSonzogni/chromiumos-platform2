// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <dbus/mock_bus.h>
#include <gtest/gtest.h>

#include "shill/event_dispatcher.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"

namespace shill {
namespace {

using testing::_;
using testing::Return;

TEST(LegacyDHCPCDListenerTest, CreateAndDestroy) {
  base::test::TaskEnvironment task_environment;
  EventDispatcher dispatcher;
  LegacyDHCPCDListenerFactory factory;

  // Create the LegacyDHCPCDListenerImpl instance.
  scoped_refptr<dbus::MockBus> mock_bus =
      new dbus::MockBus(dbus::Bus::Options());
  ON_CALL(*mock_bus, AssertOnDBusThread).WillByDefault(Return());
  ON_CALL(*mock_bus, IsConnected).WillByDefault(Return(true));
  EXPECT_CALL(*mock_bus, SetUpAsyncOperations).WillOnce(Return(true));
  EXPECT_CALL(*mock_bus, AddFilterFunction);
  EXPECT_CALL(*mock_bus,
              AddMatch("type='signal', interface='org.chromium.dhcpcd'", _));

  std::unique_ptr<LegacyDHCPCDListener> listener = factory.Create(
      mock_bus, &dispatcher, base::DoNothing(), base::DoNothing());
  testing::Mock::VerifyAndClearExpectations(mock_bus.get());

  // Destroy the LegacyDHCPCDListenerImpl instance.
  EXPECT_CALL(*mock_bus, RemoveFilterFunction);
  EXPECT_CALL(*mock_bus,
              RemoveMatch("type='signal', interface='org.chromium.dhcpcd'", _));

  listener.reset();
  testing::Mock::VerifyAndClearExpectations(mock_bus.get());
}

}  // namespace
}  // namespace shill
