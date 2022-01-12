// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/user_proximity_watcher_mojo.h"

#include <map>
#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gtest/gtest.h>
#include <libsar/test_fakes.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/system/fake_proximity.h"
#include "power_manager/powerd/system/fake_sensor_service.h"
#include "power_manager/powerd/system/proximity_events_observer.h"
#include "power_manager/powerd/system/sensor_service_handler.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager {
namespace system {

namespace {

constexpr char kActivitySysPath[] = "/sys/cros-ec-activity";
constexpr char kSarSysPath[] = "dummy";

constexpr char kProximityConfigPath[] =
    "/usr/share/chromeos-assets/proximity-sensor/bugzzy/"
    "semtech_config_cellular_wifi.json";
constexpr char kProximityConfigJson[] =
    "{\"channelConfig\": [{\"channel\" : \"0\", \"hardwaregain\" : 2, "
    "\"threshFalling\" : 1014, \"threshFallingHysteresis\" : 73, "
    "\"threshRising\" : 1014, \"threshRisingHysteresis\" : 72}], "
    "\"threshFallingPeriod\" : 2, \"threshRisingPeriod\" : 2}";

class TestObserver : public UserProximityObserver {
 public:
  struct DeviceInfo {
    std::optional<uint32_t> roles;
    std::optional<UserProximity> value;
    std::unique_ptr<base::RunLoop> loop;
  };

  explicit TestObserver(UserProximityWatcherInterface* watcher)
      : watcher_(watcher) {
    watcher_->AddObserver(this);
  }
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  ~TestObserver() override { watcher_->RemoveObserver(this); }

  // UserProximityObserver implementation:
  void OnNewSensor(int id, uint32_t roles) override {
    DeviceInfo& device = devices_[id];
    EXPECT_FALSE(device.roles.has_value());
    device.roles = roles;

    if (device.loop)
      device.loop->Quit();
  }
  void OnProximityEvent(int id, UserProximity value) override {
    DeviceInfo& device = devices_[id];
    EXPECT_TRUE(device.roles.has_value());
    device.value = value;

    if (device.loop)
      device.loop->Quit();
  }

  void SetSensorClosure(int id) {
    DeviceInfo& device = devices_[id];
    device.loop = std::make_unique<base::RunLoop>();
  }

  std::map<int, DeviceInfo> devices_;

 private:
  UserProximityWatcherInterface* watcher_;  // Not owned.
};

}  // namespace

class UserProximityWatcherMojoTest : public MojoTestEnvironment {
 public:
  UserProximityWatcherMojoTest(const UserProximityWatcherMojoTest&) = delete;
  UserProximityWatcherMojoTest& operator=(const UserProximityWatcherMojoTest&) =
      delete;

  UserProximityWatcherMojoTest() {}
  ~UserProximityWatcherMojoTest() override {}

 protected:
  void SetWatcher() {
    auto delegate =
        std::make_unique<libsar::fakes::FakeSarConfigReaderDelegate>();
    delegate->SetStringToFile(base::FilePath(kProximityConfigPath),
                              kProximityConfigJson);

    auto cros_config = std::make_unique<brillo::FakeCrosConfig>();
    cros_config->SetString("/proximity-sensor/semtech-config/0/file",
                           libsar::SarConfigReader::kSystemPathProperty,
                           kProximityConfigPath);

    watcher_ = std::make_unique<UserProximityWatcherMojo>(
        &prefs_, std::move(cros_config), std::move(delegate),
        TabletMode::UNSUPPORTED, &sensor_service_handler_);
    observer_ = std::make_unique<TestObserver>(watcher_.get());

    for (int32_t id = 0; id < sensor_num_; ++id)
      observer_->SetSensorClosure(id);

    ResetMojoChannel();
  }

  void ResetMojoChannel() {
    sensor_service_.ClearReceivers();

    mojo::PendingRemote<cros::mojom::SensorService> pending_remote;
    sensor_service_.AddReceiver(
        pending_remote.InitWithNewPipeAndPassReceiver());

    // |sensor_service_.ClearReceivers()| will trigger
    // |sensor_service_handler_::OnSensorServiceDisconnect|, if the
    // SensorService mojo pipe exists. |sensor_service_handler_::SetUpChannel|
    // should be called after the disconnect handler is executed, to setup the
    // SensorService mojo pipe again.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorServiceHandler::SetUpChannel,
                       base::Unretained(&sensor_service_handler_),
                       std::move(pending_remote), base::DoNothing()));
  }

  void SetSensor(std::string syspath,
                 std::optional<std::string> devlink = std::nullopt) {
    auto sensor_device = std::make_unique<FakeProximity>();
    sensor_device->SetAttribute(cros::mojom::kSysPath, syspath);
    if (devlink.has_value())
      sensor_device->SetAttribute(cros::mojom::kDevlink, devlink.value());

    auto iio_device_id = sensor_num_++;
    fake_proximities_[iio_device_id] = sensor_device.get();
    sensor_service_.SetSensorDevice(iio_device_id, std::move(sensor_device));
  }

  FakePrefs prefs_;

  FakeSensorService sensor_service_;
  std::map<int32_t, FakeProximity*> fake_proximities_;

  SensorServiceHandler sensor_service_handler_;

  std::unique_ptr<UserProximityWatcherMojo> watcher_;
  std::unique_ptr<TestObserver> observer_;

  int32_t sensor_num_ = 0;
};

TEST_F(UserProximityWatcherMojoTest, Basic) {
  prefs_.SetBool(kSetCellularTransmitPowerForProximityPref, false);
  prefs_.SetBool(kSetWifiTransmitPowerForProximityPref, true);
  prefs_.SetBool(kSetCellularTransmitPowerForActivityProximityPref, true);
  prefs_.SetBool(kSetWifiTransmitPowerForActivityProximityPref, true);

  SetSensor(kActivitySysPath);               // Activity sensor
  SetSensor(kSarSysPath, "proximity-wifi");  // SAR sensor

  SetWatcher();

  observer_->devices_[0].loop->Run();
  observer_->devices_[1].loop->Run();

  EXPECT_TRUE(observer_->devices_[0].roles.has_value());
  EXPECT_EQ(observer_->devices_[0].roles.value(),
            UserProximityObserver::SensorRole::SENSOR_ROLE_LTE |
                UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);
  EXPECT_TRUE(observer_->devices_[1].roles.has_value());
  EXPECT_EQ(observer_->devices_[1].roles.value(),
            UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  observer_->SetSensorClosure(0);

  fake_proximities_[0]->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_RISING, 0 /* channel */,
      0 /* timestamp */));

  // Wait until the event is updated.
  observer_->devices_[0].loop->Run();

  EXPECT_TRUE(observer_->devices_[0].value.has_value());
  EXPECT_EQ(observer_->devices_[0].value.value(), UserProximity::FAR);

  observer_->SetSensorClosure(1);

  fake_proximities_[1]->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_FALLING, 0 /* channel */,
      0 /* timestamp */));

  // Wait until the event is updated.
  observer_->devices_[1].loop->Run();

  EXPECT_TRUE(observer_->devices_[1].value.has_value());
  EXPECT_EQ(observer_->devices_[1].value.value(), UserProximity::NEAR);
}

TEST_F(UserProximityWatcherMojoTest, Disconnections) {
  prefs_.SetBool(kSetCellularTransmitPowerForProximityPref, true);
  prefs_.SetBool(kSetWifiTransmitPowerForProximityPref, true);
  prefs_.SetBool(kSetCellularTransmitPowerForActivityProximityPref, true);
  prefs_.SetBool(kSetWifiTransmitPowerForActivityProximityPref, false);

  SetSensor(kActivitySysPath);                   // Activity sensor
  SetSensor(kSarSysPath, "proximity-wifi-lte");  // SAR sensor

  SetWatcher();

  observer_->devices_[0].loop->Run();
  observer_->devices_[1].loop->Run();

  EXPECT_TRUE(observer_->devices_[0].roles.has_value());
  EXPECT_EQ(observer_->devices_[0].roles.value(),
            UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);
  EXPECT_TRUE(observer_->devices_[1].roles.has_value());
  EXPECT_EQ(observer_->devices_[1].roles.value(),
            UserProximityObserver::SensorRole::SENSOR_ROLE_LTE |
                UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  observer_->devices_.erase(0);

  fake_proximities_[0]->ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason::DEVICE_REMOVED,
      "Device was removed");

  observer_->SetSensorClosure(1);

  fake_proximities_[1]->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_FALLING, 0 /* channel */,
      0 /* timestamp */));

  // Wait until the event is updated.
  observer_->devices_[1].loop->Run();

  EXPECT_TRUE(observer_->devices_[1].value.has_value());
  EXPECT_EQ(observer_->devices_[1].value.value(), UserProximity::NEAR);

  // Reconnection to the first proximity sensor shouldn't happen.
  EXPECT_FALSE(observer_->devices_[0].roles.has_value());

  observer_->SetSensorClosure(0);
  // Simulate a disconnection between |manager_| and IIO Service.
  ResetMojoChannel();

  // Reconnection to the first proximity sensor should happen as
  // |UserProximityWatcherMojo| will query again.
  observer_->devices_[0].loop->Run();

  EXPECT_TRUE(observer_->devices_[0].roles.has_value());
  EXPECT_EQ(observer_->devices_[0].roles.value(),
            UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);
}

}  // namespace system
}  // namespace power_manager
