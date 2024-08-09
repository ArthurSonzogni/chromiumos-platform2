// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/audio_events_impl.h"

#include <memory>

#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <cras/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/events/event_observer_test_future.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::SaveArg;

// Tests for the AudioEventsImpl class.
class AudioEventsImplTest : public testing::Test {
 public:
  AudioEventsImplTest(const AudioEventsImplTest&) = delete;
  AudioEventsImplTest& operator=(const AudioEventsImplTest&) = delete;

 protected:
  AudioEventsImplTest() = default;

  void SetUp() override {
    EXPECT_CALL(*mock_context_.mock_cras_proxy(),
                DoRegisterUnderrunSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&underrun_callback_));
    EXPECT_CALL(*mock_context_.mock_cras_proxy(),
                DoRegisterSevereUnderrunSignalHandler(_, _))
        .WillOnce(SaveArg<0>(&severe_underrun_callback_));

    audio_events_impl_ = std::make_unique<AudioEventsImpl>(&mock_context_);

    audio_events_impl_->AddObserver(event_observer_.BindNewPendingRemote());
  }

  // Simulate signal is fired and then callback is run.
  void InvokeUnderrunEvent() { underrun_callback_.Run(); }
  void InvokeSevereUnderrunEvent() { severe_underrun_callback_.Run(); }

  mojom::EventInfoPtr GetNextEvent() { return event_observer_.WaitForEvent(); }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<AudioEventsImpl> audio_events_impl_;
  EventObserverTestFuture event_observer_;
  base::RepeatingClosure underrun_callback_;
  base::RepeatingClosure severe_underrun_callback_;
};

TEST_F(AudioEventsImplTest, UnderrunEvent) {
  InvokeUnderrunEvent();

  auto event_info = GetNextEvent();
  ASSERT_TRUE(event_info->is_audio_event_info());
  const auto& audio_event_info = event_info->get_audio_event_info();
  EXPECT_EQ(audio_event_info->state, mojom::AudioEventInfo::State::kUnderrun);
}

TEST_F(AudioEventsImplTest, SevereUnderrunEvent) {
  InvokeSevereUnderrunEvent();

  auto event_info = GetNextEvent();
  ASSERT_TRUE(event_info->is_audio_event_info());
  const auto& audio_event_info = event_info->get_audio_event_info();
  EXPECT_EQ(audio_event_info->state,
            mojom::AudioEventInfo::State::kSevereUnderrun);
}

}  // namespace
}  // namespace diagnostics
