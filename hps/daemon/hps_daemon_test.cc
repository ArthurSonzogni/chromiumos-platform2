// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <brillo/dbus/dbus_object_test_helpers.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <hps/daemon/hps_daemon.h>
#include <hps/dev.h>

using brillo::dbus_utils::AsyncEventSequencer;
using testing::_;
using testing::NiceMock;
using testing::Return;

namespace hps {

// TODO(slangley): We should be mocking the HPS interface itself, Dev Interface
// is to low in the stack. Do this in a follow up CL to keep this CL
// manageable.
class MockDevInterface : public DevInterface {
 public:
  MOCK_METHOD(bool, Read, (uint8_t, uint8_t*, size_t), (override));
  MOCK_METHOD(bool, Write, (uint8_t, const uint8_t*, size_t), (override));
};

class HpsDaemonTest : public testing::Test {
 public:
  HpsDaemonTest() {
    dbus::Bus::Options options;
    mock_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    dbus::ObjectPath path(::hps::kHpsServicePath);

    mock_object_proxy_ = base::MakeRefCounted<NiceMock<dbus::MockObjectProxy>>(
        mock_bus_.get(), kHpsServicePath, path);

    mock_exported_object_ =
        base::MakeRefCounted<NiceMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);

    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(testing::AnyNumber());

    auto dev_interface = std::make_unique<NiceMock<MockDevInterface>>();
    mock_dev_interface_ = dev_interface.get();

    auto hps = std::make_unique<HPS>(std::move(dev_interface));
    hps->SkipBoot();
    hps_daemon_.reset(new HpsDaemon(mock_bus_, std::move(hps)));
  }

 protected:
  bool CallEnableFeature(brillo::ErrorPtr* error_ptr, uint8_t feature) {
    return hps_daemon_->EnableFeature(error_ptr, feature);
  }

  bool CallDisableFeature(brillo::ErrorPtr* error_ptr, uint8_t feature) {
    return hps_daemon_->DisableFeature(error_ptr, feature);
  }

  bool CallGetFeatureResult(brillo::ErrorPtr* error_ptr,
                            uint8_t feature,
                            uint16_t* result) {
    return hps_daemon_->GetFeatureResult(error_ptr, feature, result);
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  NiceMock<MockDevInterface>* mock_dev_interface_;
  std::unique_ptr<HpsDaemon> hps_daemon_;
};

TEST_F(HpsDaemonTest, EnableFeatureNotReady) {
  brillo::ErrorPtr error;
  bool result = CallEnableFeature(&error, 0);
  EXPECT_FALSE(result);
  EXPECT_EQ("hpsd: Unable to enable feature", error->GetMessage());
}

TEST_F(HpsDaemonTest, DisableFeatureNotReady) {
  brillo::ErrorPtr error;
  bool result = CallDisableFeature(&error, 0);
  EXPECT_FALSE(result);
  EXPECT_EQ("hpsd: Unable to disable feature", error->GetMessage());
}

TEST_F(HpsDaemonTest, GetFeatureResultNotReady) {
  brillo::ErrorPtr error;
  uint16_t result;

  bool call_result = CallGetFeatureResult(&error, 0, &result);
  EXPECT_FALSE(call_result);
  EXPECT_EQ("hpsd: Feature result not available", error->GetMessage());
}

}  // namespace hps
