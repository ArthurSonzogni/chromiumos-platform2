// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <linux/cec-funcs.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <gmock/gmock.h>

#include "cecservice/cec_device_mock.h"
#include "cecservice/cec_fd_mock.h"
#include "cecservice/cec_manager.h"
#include "cecservice/udev_mock.h"

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace cecservice {

namespace {
void Copy(std::vector<TvPowerStatus>* out,
          const std::vector<TvPowerStatus>& in) {
  *out = in;
}
void SetTrue(bool* out) {
  *out = true;
}
}  // namespace

class CecManagerTest : public ::testing::Test {
 public:
  CecManagerTest();
  CecManagerTest(const CecManagerTest&) = delete;
  CecManagerTest& operator=(const CecManagerTest&) = delete;

  ~CecManagerTest() = default;

 protected:
  CecDeviceFactoryMock cec_factory_mock_;
  Udev::DeviceCallback device_added_callback_;
  Udev::DeviceCallback device_removed_callback_;
  std::unique_ptr<UdevMock> udev_mock_ = std::make_unique<UdevMock>();
  NiceMock<UdevFactoryMock> udev_factory_mock_;
};

CecManagerTest::CecManagerTest() {
  ON_CALL(udev_factory_mock_, Create(_, _))
      .WillByDefault(
          Invoke([&](const Udev::DeviceCallback& device_added_callback,
                     const Udev::DeviceCallback& device_removed_callback) {
            device_added_callback_ = device_added_callback;
            device_removed_callback_ = device_removed_callback;

            return std::move(udev_mock_);
          }));
}

TEST_F(CecManagerTest, TestEnumerateAndCreate) {
  std::vector<base::FilePath> devices = {base::FilePath("/dev/cec0"),
                                         base::FilePath("/dev/cec1")};
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(devices), Return(true)));

  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec0")))
      .WillOnce(Return(ByMove(std::make_unique<CecDeviceMock>())));
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec1")))
      .WillOnce(Return(ByMove(std::make_unique<CecDeviceMock>())));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);
}

TEST_F(CecManagerTest, TestAddRemoveDevice) {
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_)).WillOnce(Return(true));
  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  // Test device add.
  CecDeviceMock* device_mock = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec0")))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        device_mock = mock.get();
        return mock;
      }));
  device_added_callback_.Run(base::FilePath("/dev/cec0"));

  // Test removal.
  EXPECT_CALL(*device_mock, DestructorCalled());
  // Remove device.
  device_removed_callback_.Run(base::FilePath("/dev/cec0"));
  // Make sure that the device is now destroyed.
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(device_mock));
}

TEST_F(CecManagerTest, TestDontAddDeviceTwice) {
  // Device is added via EnumerateAndAddExistingDevices()
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(std::vector<base::FilePath>{
                          base::FilePath("/dev/cec0")}),
                      Return(true)));
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec0")))
      .WillOnce(Return(ByMove(std::make_unique<CecDeviceMock>())));
  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  // Try to add the same device via the device added callback and check we don't
  // create a new device.
  EXPECT_CALL(cec_factory_mock_, Create(_)).Times(0);
  device_added_callback_.Run(base::FilePath("/dev/cec0"));
}

TEST_F(CecManagerTest, TestPowerQuery) {
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(std::vector<base::FilePath>(1)),
                      Return(true)));

  CecDeviceMock* cec_mock = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(_))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        cec_mock = mock.get();
        return mock;
      }));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  CecDevice::GetTvPowerStatusCallback callback;
  EXPECT_CALL(*cec_mock, GetTvPowerStatus(_)).WillOnce([&callback](auto&& cb) {
    callback = std::move(cb);
  });

  std::vector<TvPowerStatus> result;
  cec_manager->GetTvsPowerStatus(base::BindOnce(Copy, &result));

  // Respond.
  std::move(callback).Run(kTvPowerStatusToStandBy);

  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(kTvPowerStatusToStandBy, result[0]);
}

TEST_F(CecManagerTest, TestWakeUp) {
  std::vector<base::FilePath> devices = {base::FilePath("/dev/cec0"),
                                         base::FilePath("/dev/cec1")};
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(devices), Return(true)));

  CecDeviceMock* device_mock_0 = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec0")))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        device_mock_0 = mock.get();
        return mock;
      }));

  CecDeviceMock* device_mock_1 = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec1")))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        device_mock_1 = mock.get();
        return mock;
      }));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  CecDevice::PowerChangeSentCallback callback_0;
  EXPECT_CALL(*device_mock_0, SetWakeUp(_)).WillOnce([&callback_0](auto&& cb) {
    callback_0 = std::move(cb);
  });
  CecDevice::PowerChangeSentCallback callback_1;
  EXPECT_CALL(*device_mock_1, SetWakeUp(_)).WillOnce([&callback_1](auto&& cb) {
    callback_1 = std::move(cb);
  });

  bool callback_called = false;
  cec_manager->SetWakeUp(base::BindOnce(SetTrue, &callback_called));

  // First device responds - callback not called yet.
  std::move(callback_0).Run();
  EXPECT_FALSE(callback_called);

  // Second device responds - callback is called.
  std::move(callback_1).Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(CecManagerTest, TestStandBy) {
  std::vector<base::FilePath> devices = {base::FilePath("/dev/cec0"),
                                         base::FilePath("/dev/cec1")};
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(devices), Return(true)));

  CecDeviceMock* device_mock_0 = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec0")))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        device_mock_0 = mock.get();
        return mock;
      }));

  CecDeviceMock* device_mock_1 = nullptr;
  EXPECT_CALL(cec_factory_mock_, Create(base::FilePath("/dev/cec1")))
      .WillOnce(Invoke([&](const base::FilePath&) {
        auto mock = std::make_unique<CecDeviceMock>();
        device_mock_1 = mock.get();
        return mock;
      }));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  CecDevice::PowerChangeSentCallback callback_0;
  EXPECT_CALL(*device_mock_0, SetStandBy(_)).WillOnce([&callback_0](auto&& cb) {
    callback_0 = std::move(cb);
  });
  CecDevice::PowerChangeSentCallback callback_1;
  EXPECT_CALL(*device_mock_1, SetStandBy(_)).WillOnce([&callback_1](auto&& cb) {
    callback_1 = std::move(cb);
  });

  bool callback_called = false;
  cec_manager->SetStandBy(base::BindOnce(SetTrue, &callback_called));

  // First device responds - callback not called yet.
  std::move(callback_0).Run();
  EXPECT_FALSE(callback_called);

  // Second device responds - callback is called.
  std::move(callback_1).Run();
  EXPECT_TRUE(callback_called);
}

TEST_F(CecManagerTest, TestDeviceRemovalWhileTvPowerQueryIsOngoing) {
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(std::vector<base::FilePath>{
                          base::FilePath("/dev/cec0")}),
                      Return(true)));

  EXPECT_CALL(cec_factory_mock_, Create(_))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<CecDeviceMock>>())));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  // We expect to get a vector of length 0 in response, in order to check for
  // that, set initial vector's length to 1.
  std::vector<TvPowerStatus> result(1);
  cec_manager->GetTvsPowerStatus(base::BindOnce(Copy, &result));

  // Remove the device.
  device_removed_callback_.Run(base::FilePath("/dev/cec0"));

  // We should get an empty answer.
  EXPECT_EQ(0u, result.size());
}

TEST_F(CecManagerTest, TestDeviceRemovalDuringStandByRequest) {
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(std::vector<base::FilePath>{
                          base::FilePath("/dev/cec0")}),
                      Return(true)));

  EXPECT_CALL(cec_factory_mock_, Create(_))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<CecDeviceMock>>())));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  bool callback_called = false;
  cec_manager->SetStandBy(base::BindOnce(SetTrue, &callback_called));

  // Remove the device - callback should be called.
  device_removed_callback_.Run(base::FilePath("/dev/cec0"));
  EXPECT_TRUE(callback_called);
}

TEST_F(CecManagerTest, TestDeviceRemovalDuringWakeUpRequest) {
  EXPECT_CALL(*udev_mock_, EnumerateDevices(_))
      .WillOnce(DoAll(SetArgPointee<0>(std::vector<base::FilePath>{
                          base::FilePath("/dev/cec0")}),
                      Return(true)));

  EXPECT_CALL(cec_factory_mock_, Create(_))
      .WillOnce(Return(ByMove(std::make_unique<NiceMock<CecDeviceMock>>())));

  auto cec_manager =
      std::make_unique<CecManager>(udev_factory_mock_, cec_factory_mock_);

  bool callback_called = false;
  cec_manager->SetWakeUp(base::BindOnce(SetTrue, &callback_called));

  // Remove the device - callback should be called.
  device_removed_callback_.Run(base::FilePath("/dev/cec0"));
  EXPECT_TRUE(callback_called);
}

}  // namespace cecservice
