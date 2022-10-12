// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>

#include <base/check.h>
#include <base/memory/ref_counted.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"

using ::testing::_;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace diagnostics {
namespace {

class BasePowerdAdapterImplTest : public ::testing::Test {
 public:
  BasePowerdAdapterImplTest()
      : dbus_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        dbus_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            dbus_bus_.get(),
            power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath))) {}
  BasePowerdAdapterImplTest(const BasePowerdAdapterImplTest&) = delete;
  BasePowerdAdapterImplTest& operator=(const BasePowerdAdapterImplTest&) =
      delete;

  void SetUp() override {
    EXPECT_CALL(*dbus_bus_,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(Return(dbus_object_proxy_.get()));

    powerd_adapter_ = std::make_unique<PowerdAdapterImpl>(dbus_bus_);
  }

  PowerdAdapterImpl* powerd_adapter() const {
    DCHECK(powerd_adapter_);
    return powerd_adapter_.get();
  }

  dbus::MockObjectProxy* mock_dbus_object_proxy() const {
    DCHECK(dbus_object_proxy_);
    return dbus_object_proxy_.get();
  }

 private:
  scoped_refptr<StrictMock<dbus::MockBus>> dbus_bus_;

  scoped_refptr<StrictMock<dbus::MockObjectProxy>> dbus_object_proxy_;

  std::unique_ptr<PowerdAdapterImpl> powerd_adapter_;
};

TEST_F(BasePowerdAdapterImplTest, PowerSupplySuccess) {
  power_manager::PowerSupplyProperties power_supply_proto;
  EXPECT_CALL(*mock_dbus_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([&power_supply_proto](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> power_manager_response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter power_manager_writer(power_manager_response.get());
        power_manager_writer.AppendProtoAsArrayOfBytes(power_supply_proto);
        return power_manager_response;
      });

  auto response = powerd_adapter()->GetPowerSupplyProperties();
  EXPECT_TRUE(response);
  // The proto structure is simple enough where it can be compared as a string.
  // If if becomes more complex this will need to change.
  EXPECT_EQ(response.value().SerializeAsString(),
            power_supply_proto.SerializeAsString());
}

TEST_F(BasePowerdAdapterImplTest, PowerSupplyFail) {
  power_manager::PowerSupplyProperties power_supply_proto;
  EXPECT_CALL(*mock_dbus_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  ASSERT_EQ(powerd_adapter()->GetPowerSupplyProperties(), std::nullopt);
}

TEST_F(BasePowerdAdapterImplTest, PowerSupplyParseError) {
  power_manager::PowerSupplyProperties power_supply_proto;
  EXPECT_CALL(*mock_dbus_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  ASSERT_EQ(powerd_adapter()->GetPowerSupplyProperties(), std::nullopt);
}

}  // namespace
}  // namespace diagnostics
