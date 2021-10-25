// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "cras/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/events/audio_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::StrictMock;

class MockAudioObserver
    : public chromeos::cros_healthd::mojom::CrosHealthdAudioObserver {
 public:
  explicit MockAudioObserver(
      mojo::PendingReceiver<
          chromeos::cros_healthd::mojom::CrosHealthdAudioObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockAudioObserver(const MockAudioObserver&) = delete;
  MockAudioObserver& operator=(const MockAudioObserver&) = delete;

  MOCK_METHOD(void, OnUnderrun, (), (override));
  MOCK_METHOD(void, OnSevereUnderrun, (), (override));

 private:
  mojo::Receiver<chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
      receiver_;
};

// Tests for the AudioEventsImpl class.
class AudioEventsImplTest : public testing::Test {
 protected:
  AudioEventsImplTest() = default;
  AudioEventsImplTest(const AudioEventsImplTest&) = delete;
  AudioEventsImplTest& operator=(const AudioEventsImplTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_context_.mock_cras_proxy(),
                DoRegisterUnderrunSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&underrun_callback_));
    EXPECT_CALL(*mock_context_.mock_cras_proxy(),
                DoRegisterSevereUnderrunSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&severe_underrun_callback_));

    audio_events_impl_ = std::make_unique<AudioEventsImpl>(&mock_context_);

    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
        observer;
    observer_ = std::make_unique<StrictMock<MockAudioObserver>>(
        observer.InitWithNewPipeAndPassReceiver());
    audio_events_impl_->AddObserver(std::move(observer));
  }

  // Simulate signal is fired and then callback is run.
  void InvokeUnderrunEvent() { underrun_callback_.Run(); }
  void InvokeSevereUnderrunEvent() { severe_underrun_callback_.Run(); }

  MockAudioObserver* mock_observer() { return observer_.get(); }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<AudioEventsImpl> audio_events_impl_;
  std::unique_ptr<StrictMock<MockAudioObserver>> observer_;
  base::Callback<void()> underrun_callback_;
  base::Callback<void()> severe_underrun_callback_;
};

TEST_F(AudioEventsImplTest, UnderrunEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnUnderrun()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  InvokeUnderrunEvent();

  run_loop.Run();
}

TEST_F(AudioEventsImplTest, SevereUnderrunEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnSevereUnderrun()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  InvokeSevereUnderrunEvent();

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
