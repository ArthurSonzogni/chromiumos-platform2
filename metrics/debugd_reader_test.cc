// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <memory>
#include <utility>

#include <chromeos/dbus/service_constants.h>
#include <base/logging.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_proxy.h>
#include <gtest/gtest.h>

#include "metrics/debugd_reader.h"

using ::testing::_;
using ::testing::Optional;

namespace chromeos_metrics {

const char kTestMessage[] = "Lorem ipsum dolor sit amet";

class DebugdReaderTest : public testing::Test {
 public:
  DebugdReaderTest() = default;

  void SetUp() override {
    dbus::Bus::Options options;

    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);

    proxy_ =
        new dbus::MockObjectProxy(bus_.get(), debugd::kDebugdServiceName,
                                  dbus::ObjectPath(debugd::kDebugdServicePath));

    // Our MockResponse should be used by the tested class.
    EXPECT_CALL(*proxy_.get(), CallMethodAndBlock(_, _))
        .WillRepeatedly(testing::Invoke(this, &DebugdReaderTest::MockResponse));

    // DebugdReader constructor should get our mocked ObjectProxy.
    EXPECT_CALL(*bus_.get(),
                GetObjectProxy(debugd::kDebugdServiceName,
                               dbus::ObjectPath(debugd::kDebugdServicePath)))
        .WillOnce(testing::Return(proxy_.get()));
  }

 protected:
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  scoped_refptr<dbus::MockBus> bus_;

  std::string log_name_;

 private:
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> MockResponse(
      dbus::MethodCall* call, int timeout_ms) {
    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    dbus::MessageReader reader(call);
    std::string message;

    if (call->GetInterface().compare(debugd::kDebugdInterface) ||
        call->GetMember().compare(debugd::kGetLog)) {
      return base::unexpected(
          dbus::Error(DBUS_ERROR_NOT_SUPPORTED, "Not implemented"));
    }

    if (reader.GetDataType() != DBUS_TYPE_STRING) {
      return base::unexpected(
          dbus::Error(DBUS_ERROR_INVALID_ARGS, "Invalid input type"));
    }

    if (!reader.PopString(&message)) {
      LOG(ERROR) << "Failed to extract input string";
      return base::unexpected(dbus::Error());
    }

    // Follow debugd behavior. If LogName is as expected return TestMessage.
    // Otherwise return an empty string to signal that no such log exists.
    if (!log_name_.compare(message)) {
      writer.AppendString(kTestMessage);
    } else {
      writer.AppendString("");
    }

    return base::ok(std::move(response));
  }
};

TEST_F(DebugdReaderTest, LogNameBadCall) {
  DebugdReader reader(bus_.get(), "test0");
  log_name_ = "test1";

  EXPECT_EQ(reader.Read(), std::nullopt);
}

TEST_F(DebugdReaderTest, LogNameGoodCall) {
  DebugdReader reader(bus_.get(), "test0");
  log_name_ = "test0";

  std::optional<std::string> log = reader.Read();
  EXPECT_THAT(log, Optional(std::string(kTestMessage)));
}
}  // namespace chromeos_metrics
