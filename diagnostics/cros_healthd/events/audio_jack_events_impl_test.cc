// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/events/audio_jack_events_impl.h"
#include "diagnostics/cros_healthd/events/mock_event_observer.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArg;

// Tests for the AudioJackEventsImpl class.
class AudioJackEventsImplTest : public testing::Test {
 protected:
  AudioJackEventsImplTest() = default;
  AudioJackEventsImplTest(const AudioJackEventsImplTest&) = delete;
  AudioJackEventsImplTest& operator=(const AudioJackEventsImplTest&) = delete;

  void SetUp() override {
    audio_jack_events_impl_ =
        std::make_unique<AudioJackEventsImpl>(&mock_context_);

    mojo::PendingRemote<mojom::EventObserver> audio_jack_observer;
    mojo::PendingReceiver<mojom::EventObserver> observer_receiver(
        audio_jack_observer.InitWithNewPipeAndPassReceiver());
    event_observer_ = std::make_unique<StrictMock<MockEventObserver>>(
        std::move(observer_receiver));
    SetExecutorMonitorAudioJack();
    audio_jack_events_impl_->AddObserver(std::move(audio_jack_observer));
  }

  MockEventObserver* mock_event_observer() { return event_observer_.get(); }
  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void SetExecutorMonitorAudioJack() {
    EXPECT_CALL(*mock_executor(), MonitorAudioJack(_, _))
        .WillOnce(WithArg<0>([=](mojo::PendingRemote<mojom::AudioJackObserver>
                                     audio_jack_observer) {
          audio_jack_observer_.Bind(std::move(audio_jack_observer));
        }));
  }

  void EmitAudioJackAddEvent() { audio_jack_observer_->OnAdd(); }

  void EmitAudioJackRemoveEvent() { audio_jack_observer_->OnRemove(); }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockEventObserver>> event_observer_;
  std::unique_ptr<AudioJackEventsImpl> audio_jack_events_impl_;
  mojo::Remote<mojom::AudioJackObserver> audio_jack_observer_;
};

// Test that we can receive audio jack add events.
TEST_F(AudioJackEventsImplTest, AudioJackAddEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_audio_jack_event_info());
        const auto& audio_jack_event_info = info->get_audio_jack_event_info();
        EXPECT_EQ(audio_jack_event_info->state,
                  mojom::AudioJackEventInfo::State::kAdd);
        run_loop.Quit();
      }));

  EmitAudioJackAddEvent();

  run_loop.Run();
}

// Test that we can receive audio jack remove events.
TEST_F(AudioJackEventsImplTest, AudioJackRemoveEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_event_observer(), OnEvent(_))
      .WillOnce(Invoke([&](mojom::EventInfoPtr info) {
        EXPECT_TRUE(info->is_audio_jack_event_info());
        const auto& audio_jack_event_info = info->get_audio_jack_event_info();
        EXPECT_EQ(audio_jack_event_info->state,
                  mojom::AudioJackEventInfo::State::kRemove);
        run_loop.Quit();
      }));

  EmitAudioJackRemoveEvent();

  run_loop.Run();
}

}  // namespace
}  // namespace diagnostics
