// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/patchpanel_adaptor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <dbus/dbus-protocol.h>
#include <dbus/error.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::Invoke;
using testing::Return;

namespace patchpanel {
namespace {

std::unique_ptr<dbus::Response> CreateUidResponse(dbus::MethodCall* method_call,
                                                  uint32_t uid) {
  method_call->SetSerial(1);
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendUint32(uid);
  return response;
}

}  // namespace

class TestMockBus : public dbus::MockBus {
 public:
  explicit TestMockBus(dbus::Bus::Options options)
      : dbus::MockBus(std::move(options)) {}

  MOCK_METHOD(std::string,
              GetServiceOwnerAndBlock,
              (const std::string&, dbus::Bus::GetServiceOwnerOption),
              (override));
};

class PatchpanelAdaptorTest : public testing::Test {
 public:
  PatchpanelAdaptorTest() {
    mock_bus_ = new TestMockBus(dbus::Bus::Options());
    mock_dbus_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), DBUS_SERVICE_DBUS, dbus::ObjectPath(DBUS_PATH_DBUS));
  }

 protected:
  scoped_refptr<TestMockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_dbus_proxy_;
};

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_AllowedConciergeSender) {
  const std::string kConciergeOwner = ":1.100";
  const std::string kSender = ":1.100";

  EXPECT_CALL(*mock_bus_, GetServiceOwnerAndBlock(
                              vm_tools::concierge::kVmConciergeServiceName,
                              dbus::Bus::SUPPRESS_ERRORS))
      .WillOnce(Return(kConciergeOwner));

  std::unique_ptr<dbus::MethodCall> method_call =
      std::make_unique<dbus::MethodCall>("org.chromium.Patchpanel",
                                         "ArcVmStartup");
  method_call->SetSender(kSender);

  brillo::ErrorPtr error;
  EXPECT_TRUE(CheckConciergeSender(method_call.get(), mock_bus_.get(), &error));
  EXPECT_EQ(error.get(), nullptr);
}

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_AllowedRootSender) {
  const std::string kConciergeOwner = ":1.100";
  const std::string kSender = ":1.200";

  EXPECT_CALL(*mock_bus_, GetServiceOwnerAndBlock(
                              vm_tools::concierge::kVmConciergeServiceName,
                              dbus::Bus::SUPPRESS_ERRORS))
      .WillOnce(Return(kConciergeOwner));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(DBUS_SERVICE_DBUS,
                                         dbus::ObjectPath(DBUS_PATH_DBUS)))
      .WillOnce(Return(mock_dbus_proxy_.get()));
  EXPECT_CALL(*mock_dbus_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Invoke([](dbus::MethodCall* method_call, int /*timeout_ms*/) {
        EXPECT_EQ(method_call->GetInterface(), DBUS_INTERFACE_DBUS);
        EXPECT_EQ(method_call->GetMember(), "GetConnectionUnixUser");
        return base::ok(CreateUidResponse(method_call, 0));
      }));

  std::unique_ptr<dbus::MethodCall> method_call =
      std::make_unique<dbus::MethodCall>("org.chromium.Patchpanel",
                                         "TerminaVmStartup");
  method_call->SetSender(kSender);

  brillo::ErrorPtr error;
  EXPECT_TRUE(CheckConciergeSender(method_call.get(), mock_bus_.get(), &error));
  EXPECT_EQ(error.get(), nullptr);
}

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_DeniedNonRootSender) {
  const std::string kConciergeOwner = ":1.100";
  const std::string kSender = ":1.200";
  constexpr uint32_t kCrosvmUid = 299;

  EXPECT_CALL(*mock_bus_, GetServiceOwnerAndBlock(
                              vm_tools::concierge::kVmConciergeServiceName,
                              dbus::Bus::SUPPRESS_ERRORS))
      .WillOnce(Return(kConciergeOwner));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(DBUS_SERVICE_DBUS,
                                         dbus::ObjectPath(DBUS_PATH_DBUS)))
      .WillOnce(Return(mock_dbus_proxy_.get()));
  EXPECT_CALL(*mock_dbus_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Invoke([](dbus::MethodCall* method_call, int /*timeout_ms*/) {
        return base::ok(CreateUidResponse(method_call, kCrosvmUid));
      }));

  std::unique_ptr<dbus::MethodCall> method_call =
      std::make_unique<dbus::MethodCall>("org.chromium.Patchpanel",
                                         "ArcVmStartup");
  method_call->SetSender(kSender);

  brillo::ErrorPtr error;
  EXPECT_FALSE(
      CheckConciergeSender(method_call.get(), mock_bus_.get(), &error));
  ASSERT_NE(error.get(), nullptr);
  EXPECT_EQ(error->GetCode(), DBUS_ERROR_ACCESS_DENIED);
}

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_BusError) {
  EXPECT_CALL(*mock_bus_, GetServiceOwnerAndBlock(
                              vm_tools::concierge::kVmConciergeServiceName,
                              dbus::Bus::SUPPRESS_ERRORS))
      .WillOnce(Return(""));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(DBUS_SERVICE_DBUS,
                                         dbus::ObjectPath(DBUS_PATH_DBUS)))
      .WillOnce(Return(mock_dbus_proxy_.get()));
  EXPECT_CALL(*mock_dbus_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(Return(base::unexpected(
          dbus::Error(DBUS_ERROR_FAILED, "GetConnectionUnixUser failed"))));

  std::unique_ptr<dbus::MethodCall> method_call =
      std::make_unique<dbus::MethodCall>("org.chromium.Patchpanel",
                                         "ArcVmStartup");
  method_call->SetSender(":1.200");

  brillo::ErrorPtr error;
  EXPECT_FALSE(
      CheckConciergeSender(method_call.get(), mock_bus_.get(), &error));
  ASSERT_NE(error.get(), nullptr);
  EXPECT_EQ(error->GetCode(), DBUS_ERROR_ACCESS_DENIED);
}

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_EmptySenderDenied) {
  ON_CALL(*mock_bus_,
          GetServiceOwnerAndBlock(vm_tools::concierge::kVmConciergeServiceName,
                                  dbus::Bus::SUPPRESS_ERRORS))
      .WillByDefault(Return(""));

  std::unique_ptr<dbus::MethodCall> method_call =
      std::make_unique<dbus::MethodCall>("org.chromium.Patchpanel",
                                         "ArcVmStartup");

  brillo::ErrorPtr error;
  EXPECT_FALSE(
      CheckConciergeSender(method_call.get(), mock_bus_.get(), &error));
  ASSERT_NE(error.get(), nullptr);
  EXPECT_EQ(error->GetCode(), DBUS_ERROR_INVALID_ARGS);
}

TEST_F(PatchpanelAdaptorTest, CheckConciergeSender_MissingMessageDenied) {
  brillo::ErrorPtr error;
  EXPECT_FALSE(CheckConciergeSender(nullptr, mock_bus_.get(), &error));
  ASSERT_NE(error.get(), nullptr);
  EXPECT_EQ(error->GetCode(), DBUS_ERROR_INVALID_ARGS);
}

}  // namespace patchpanel
