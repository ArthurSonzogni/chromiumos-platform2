// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/proximity_events_observer.h"

#include <memory>
#include <utility>

#include <base/notreached.h>
#include <optional>
#include <base/run_loop.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/ambient_light_observer.h"
#include "power_manager/powerd/system/fake_proximity.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager {
namespace system {

namespace {

const int kFakeSensorId = 1;
const int kEnabledEventIndex = 0;

class TestObserver : public UserProximityObserver {
 public:
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  TestObserver() = default;
  ~TestObserver() override = default;

  void WaitUntilEventReceived() {
    base::RunLoop loop;
    closure_ = loop.QuitClosure();
    loop.Run();
  }

  // UserProximityObserver implementation:
  void OnNewSensor(int id, uint32_t roles) override {
    NOTREACHED() << "TestObserver::OnNewSensor shouldn't be called.";
  }
  void OnProximityEvent(int id, UserProximity value) override {
    EXPECT_EQ(kFakeSensorId, id);
    value_ = value;

    if (closure_)
      closure_.Run();
  }

  std::optional<UserProximity> value_;

 private:
  base::RepeatingClosure closure_;
};

}  // namespace

class ProximityEventsObserverTest : public MojoTestEnvironment {
 public:
  ProximityEventsObserverTest(const ProximityEventsObserverTest&) = delete;
  ProximityEventsObserverTest& operator=(const ProximityEventsObserverTest&) =
      delete;

  ProximityEventsObserverTest() = default;
  ~ProximityEventsObserverTest() override = default;

 protected:
  void SetUp() override {
    observers_.AddObserver(&observer_);

    fake_proximity_ = std::make_unique<FakeProximity>();
    mojo::Remote<cros::mojom::SensorDevice> remote;
    id_ = fake_proximity_->AddReceiver(remote.BindNewPipeAndPassReceiver());

    proximity_events_observer_ = std::make_unique<ProximityEventsObserver>(
        kFakeSensorId, std::vector<int>{kEnabledEventIndex}, std::move(remote),
        &observers_);
  }

  void TearDown() override { observers_.RemoveObserver(&observer_); }

  TestObserver observer_;
  base::ObserverList<UserProximityObserver> observers_;

  std::unique_ptr<FakeProximity> fake_proximity_;

  std::unique_ptr<ProximityEventsObserver> proximity_events_observer_;

  mojo::ReceiverId id_;
};

TEST_F(ProximityEventsObserverTest, Basic) {
  fake_proximity_->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_RISING, 0 /* channel */,
      0 /* timestamp */));

  observer_.WaitUntilEventReceived();
  EXPECT_EQ(observer_.value_.value(), UserProximity::FAR);

  // |proximity_events_observer_| won't receive this.
  fake_proximity_->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_RISING, 1 /* channel */,
      0 /* timestamp */));

  fake_proximity_->OnEventUpdated(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_FALLING, 0 /* channel */,
      0 /* timestamp */));

  observer_.WaitUntilEventReceived();
  EXPECT_EQ(observer_.value_.value(), UserProximity::NEAR);

  // Simulate disconnection of the observer channel.
  fake_proximity_->ResetAllEventsObserverRemotes();
}

}  // namespace system
}  // namespace power_manager
