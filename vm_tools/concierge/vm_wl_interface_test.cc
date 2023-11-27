// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_wl_interface.h"

#include <memory>
#include <utility>

#include <dbus/error.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/vm_wl/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vm_applications/apps.pb.h>

namespace vm_tools::concierge {

namespace {

using testing::_;
using testing::A;

dbus::Bus::Options GetDbusOptions() {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  return opts;
}

class VmWlInterfaceTest : public testing::Test {
 public:
  VmWlInterfaceTest()
      : mock_bus_(new dbus::MockBus(GetDbusOptions())),
        mock_proxy_(
            new dbus::MockObjectProxy(mock_bus_.get(),
                                      wl::kVmWlServiceName,
                                      dbus::ObjectPath(wl::kVmWlServicePath))) {
    EXPECT_CALL(*mock_bus_.get(),
                GetObjectProxy(wl::kVmWlServiceName,
                               dbus::ObjectPath(wl::kVmWlServicePath)))
        .WillRepeatedly(testing::Return(mock_proxy_.get()));
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
};

}  // namespace

TEST_F(VmWlInterfaceTest, FailureReturnsNullptr) {
  EXPECT_CALL(*mock_proxy_.get(),
              CallMethodAndBlock(A<dbus::MethodCall*>(), A<int>()))
      .WillOnce(testing::Invoke([](dbus::MethodCall* method_call,
                                   int timeout_ms) {
        EXPECT_EQ(method_call->GetMember(),
                  wl::kVmWlServiveListenOnSocketMethod);
        return base::unexpected(dbus::Error(DBUS_ERROR_FAILED, "test error"));
      }));

  VmId id("test_owner_id", "test_vm_name");
  VmWlInterface::Result socket = VmWlInterface::CreateWaylandServer(
      mock_bus_.get(), id, apps::VmType::UNKNOWN);
  EXPECT_FALSE(socket.has_value());
}

TEST_F(VmWlInterfaceTest, SuccessfulCreateAndDestroy) {
  EXPECT_CALL(*mock_proxy_.get(),
              CallMethodAndBlock(A<dbus::MethodCall*>(), A<int>()))
      .WillOnce(
          testing::Invoke([](dbus::MethodCall* method_call, int timeout_ms) {
            EXPECT_EQ(method_call->GetMember(),
                      wl::kVmWlServiveListenOnSocketMethod);
            return base::ok(dbus::Response::CreateEmpty());
          }));

  VmId id("test_owner_id", "test_vm_name");

  VmWlInterface::Result socket = VmWlInterface::CreateWaylandServer(
      mock_bus_.get(), id, apps::VmType::UNKNOWN);
  EXPECT_TRUE(socket.has_value());

  EXPECT_CALL(*mock_proxy_.get(),
              CallMethodAndBlock(A<dbus::MethodCall*>(), A<int>()))
      .WillOnce(testing::Invoke([](dbus::MethodCall* method_call,
                                   int timeout_ms) {
        EXPECT_EQ(method_call->GetMember(), wl::kVmWlServiceCloseSocketMethod);
        return base::ok(dbus::Response::CreateEmpty());
      }));
  socket.value().reset();
}

}  // namespace vm_tools::concierge
