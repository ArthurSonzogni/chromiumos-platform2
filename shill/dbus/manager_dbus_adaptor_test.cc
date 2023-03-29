// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/manager_dbus_adaptor.h"

#include <memory>

#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;
using testing::WithArg;

namespace shill {

class ManagerDBusAdaptorTest : public Test {
 public:
  ManagerDBusAdaptorTest()
      : adaptor_bus_(new dbus::MockBus(dbus::Bus::Options())),
        proxy_bus_(new dbus::MockBus(dbus::Bus::Options())),
        manager_(&control_interface_, &dispatcher_, &metrics_),
        manager_adaptor_(adaptor_bus_, proxy_bus_, &manager_) {}

  ~ManagerDBusAdaptorTest() override = default;

  void SetUp() override {}

  void TearDown() override {}

 protected:
  scoped_refptr<dbus::MockBus> adaptor_bus_;
  scoped_refptr<dbus::MockBus> proxy_bus_;
  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  ManagerDBusAdaptor manager_adaptor_;
};

void SetErrorTypeSuccess(Error* error) {
  error->Populate(Error::kSuccess);
}

void SetErrorTypeFailure(Error* error) {
  error->Populate(Error::kOperationFailed);
}

TEST_F(ManagerDBusAdaptorTest, ClaimInterface) {
  brillo::ErrorPtr error;
  std::string kDefaultClaimerName = "";
  std::string kNonDefaultClaimerName = "test_claimer";
  std::string kInterfaceName = "test_interface";
  std::unique_ptr<dbus::Response> message(dbus::Response::CreateEmpty());

  // Watcher for device claimer is not created when we fail to claim the device.
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeFailure)));
  manager_adaptor_.ClaimInterface(&error, message.get(), kNonDefaultClaimerName,
                                  kInterfaceName);

  // Watcher for device claimer is not created when we succeed in claiming the
  // device from the default claimer.
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeSuccess)));
  manager_adaptor_.ClaimInterface(&error, message.get(), kDefaultClaimerName,
                                  kInterfaceName);

  // Watcher for device claimer is created when we succeed in claiming the
  // device from a non-default claimer.
  EXPECT_CALL(manager_, ClaimDevice(_, kInterfaceName, _))
      .WillOnce(WithArg<2>(Invoke(SetErrorTypeSuccess)));
  manager_adaptor_.ClaimInterface(&error, message.get(), kNonDefaultClaimerName,
                                  kInterfaceName);
}

TEST_F(ManagerDBusAdaptorTest, ReleaseInterface) {
  brillo::ErrorPtr error;
  std::string kClaimerName = "test_claimer";
  std::string kInterfaceName = "test_interface";
  std::unique_ptr<dbus::Response> message(dbus::Response::CreateEmpty());

  // If the device claimer is not removed, do not reset the watcher for device
  // claimer.
  EXPECT_CALL(manager_, ReleaseDevice(_, kInterfaceName, _, _))
      .WillOnce(SetArgPointee<2>(false));
  manager_adaptor_.ReleaseInterface(&error, message.get(), kClaimerName,
                                    kInterfaceName);

  // If the device claimer is removed, reset the watcher for device claimer.
  EXPECT_CALL(manager_, ReleaseDevice(_, kInterfaceName, _, _))
      .WillOnce(SetArgPointee<2>(true));
  manager_adaptor_.ReleaseInterface(&error, message.get(), kClaimerName,
                                    kInterfaceName);
}

}  // namespace shill
