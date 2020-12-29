// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/notreached.h>
#include <base/rand_util.h>
#include <base/test/task_environment.h>
#include <libmems/test_fakes.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "iioservice/daemon/events_handler.h"
#include "iioservice/daemon/test_fakes.h"

namespace iioservice {

namespace {

constexpr int kNumFailures = 10;

// An observer that does nothing to events or errors. Instead, it simply waits
// for the mojo disconnection and calls |quit_closure|.
class FakeObserver : cros::mojom::SensorDeviceEventsObserver {
 public:
  explicit FakeObserver(base::RepeatingClosure quit_closure)
      : quit_closure_(std::move(quit_closure)) {}

  mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> GetRemote() {
    CHECK(!receiver_.is_bound());
    auto pending_remote = receiver_.BindNewPipeAndPassRemote();
    receiver_.set_disconnect_handler(base::BindOnce(
        &FakeObserver::OnObserverDisconnect, base::Unretained(this)));
    return pending_remote;
  }

  // cros::mojom::SensorDeviceEventsObserver overrides:
  void OnEventUpdated(cros::mojom::IioEventPtr event) override {}
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override {}

 private:
  void OnObserverDisconnect() {
    receiver_.reset();
    quit_closure_.Run();
  }

  base::RepeatingClosure quit_closure_;
  mojo::Receiver<cros::mojom::SensorDeviceEventsObserver> receiver_{this};
};

class EventsHandlerTest : public ::testing::Test,
                          public cros::mojom::SensorDeviceEventsObserver {
 public:
  mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> GetRemote() {
    mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> remote;
    receiver_set_.Add(this, remote.InitWithNewPipeAndPassReceiver());
    return remote;
  }

  // cros::mojom::SensorDeviceEventsObserver overrides:
  void OnEventUpdated(cros::mojom::IioEventPtr event) override { NOTREACHED(); }
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override {
    NOTREACHED();
  }

 protected:
  void SetUp() override {
    device_ =
        std::make_unique<libmems::fakes::FakeIioDevice>(nullptr, "sx9310", 0);

    for (int i = 0; i < 4; ++i) {
      device_->AddEvent(std::make_unique<libmems::fakes::FakeIioEvent>(
          iio_chan_type::IIO_PROXIMITY, iio_event_type::IIO_EV_TYPE_THRESH,
          iio_event_direction::IIO_EV_DIR_EITHER, i));
    }

    handler_ = EventsHandler::Create(
        task_environment_.GetMainThreadTaskRunner(),
        task_environment_.GetMainThreadTaskRunner(), device_.get());
    EXPECT_TRUE(handler_);
  }

  void TearDown() override {
    handler_.reset();
    observers_.clear();

    base::RunLoop().RunUntilIdle();

    // ClientData should be valid until |handler_| is destructed.
    clients_data_.clear();
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};

  std::unique_ptr<libmems::fakes::FakeIioDevice> device_;

  EventsHandler::ScopedEventsHandler handler_ = {
      nullptr, EventsHandler::EventsHandlerDeleter};
  std::unique_ptr<DeviceData> device_data_;
  std::vector<ClientData> clients_data_;
  std::vector<std::unique_ptr<fakes::FakeEventsObserver>> observers_;
  mojo::ReceiverSet<cros::mojom::SensorDeviceEventsObserver> receiver_set_;
};

TEST_F(EventsHandlerTest, AddClientAndRemoveClient) {
  // No events in this test
  device_->SetPauseCallbackAtKthEvents(0, base::BindOnce([]() {}));

  device_data_ = std::make_unique<DeviceData>(
      device_.get(),
      std::set<cros::mojom::DeviceType>{cros::mojom::DeviceType::PROXIMITY});

  // ClientData should be valid until |handler_| is destructed.
  clients_data_.emplace_back(ClientData(0, device_data_.get()));
  ClientData& client_data = clients_data_[0];

  client_data.enabled_event_indices.emplace(3);  // timestamp

  handler_->AddClient(&client_data, GetRemote());
  {
    base::RunLoop run_loop;
    FakeObserver observer(run_loop.QuitClosure());
    handler_->AddClient(&client_data, observer.GetRemote());
    // Wait until |Observer| is disconnected.
    run_loop.Run();
  }
  {
    base::RunLoop run_loop;
    handler_->RemoveClient(&client_data, run_loop.QuitClosure());
    run_loop.Run();
  }
  {  // RemoveClient can be called multiple times.
    base::RunLoop run_loop;
    handler_->RemoveClient(&client_data, run_loop.QuitClosure());
    run_loop.Run();
  }
}

// Add clients with only the event with channel 3 enabled, enable all other
// channels, and disable all channels except for event with channel 0. Enabled
// channels are checked after each modification.
TEST_F(EventsHandlerTest, UpdateEventsEnabled) {
  // No events in this test
  device_->SetPauseCallbackAtKthEvents(0, base::BindOnce([]() {}));

  device_data_ = std::make_unique<DeviceData>(
      device_.get(),
      std::set<cros::mojom::DeviceType>{cros::mojom::DeviceType::PROXIMITY});

  clients_data_.emplace_back(ClientData(0, device_data_.get()));
  ClientData& client_data = clients_data_[0];

  // At least one channel enabled
  client_data.enabled_event_indices.emplace(3);

  handler_->AddClient(&client_data, GetRemote());

  handler_->UpdateEventsEnabled(
      &client_data, std::vector<int32_t>{1, 0, 2}, true,
      base::BindOnce([](const std::vector<int32_t>& failed_indices) {
        EXPECT_TRUE(failed_indices.empty());
      }));

  handler_->GetEventsEnabled(
      &client_data, std::vector<int32_t>{1, 0, 2, 3},
      base::BindOnce([](const std::vector<bool>& enabled) {
        EXPECT_EQ(enabled.size(), 4);
        EXPECT_TRUE(enabled[0] && enabled[1] && enabled[2] && enabled[3]);
      }));

  handler_->UpdateEventsEnabled(
      &client_data, std::vector<int32_t>{2, 1, 3}, false,
      base::BindOnce(
          [](ClientData* client_data, int32_t chn_index,
             const std::vector<int32_t>& failed_indices) {
            EXPECT_EQ(client_data->enabled_event_indices.size(), 1);
            EXPECT_EQ(*client_data->enabled_event_indices.begin(), chn_index);
            EXPECT_TRUE(failed_indices.empty());
          },
          &client_data, 0));

  handler_->RemoveClient(&client_data, base::DoNothing());
}

// Add all clients into the event handler, and read all events. All events are
// checked when received by observers.
TEST_F(EventsHandlerTest, ReadEventsWithEnabledFakeIioEvents) {
  // Set the pause in the beginning to prevent reading events before all
  // clients added.
  device_->SetPauseCallbackAtKthEvents(0, base::BindOnce([]() {}));

  device_data_ = std::make_unique<DeviceData>(
      device_.get(),
      std::set<cros::mojom::DeviceType>{cros::mojom::DeviceType::PROXIMITY});

  std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures;
  for (int i = 0; i < kNumFailures; ++i) {
    int k = base::RandInt(0, libmems::fakes::kEventNumber - 1);

    device_->AddFailedReadAtKthEvent(k);
    failures.insert(
        std::make_pair(k, cros::mojom::ObserverErrorType::READ_FAILED));
  }

  std::vector<std::set<int32_t>> clients = {
      {0, 1},
      {0},
  };
  clients_data_.reserve(clients.size());

  for (size_t i = 0; i < clients.size(); ++i) {
    clients_data_.emplace_back(ClientData(i, device_data_.get()));
    ClientData& client_data = clients_data_[i];

    client_data.enabled_event_indices = clients[i];

    // The fake observer needs |max_freq| and |max_freq2| to calculate the
    // correct values of events
    auto fake_observer = std::make_unique<fakes::FakeEventsObserver>(
        device_.get(), failures, clients[i]);

    handler_->AddClient(&client_data, fake_observer->GetRemote());

    observers_.emplace_back(std::move(fake_observer));
  }

  // TODO(chenghaoyang): pause and enable other FakeIioEvents.

  device_->ResumeReadingEvents();

  // Wait until all observers receive all events.
  base::RunLoop().RunUntilIdle();

  for (const auto& observer : observers_)
    EXPECT_TRUE(observer->FinishedObserving());

  // Remove clients
  for (auto& client_data : clients_data_)
    handler_->RemoveClient(&client_data, base::DoNothing());
}

}  // namespace

}  // namespace iioservice
