// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include <base/check.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "cras/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/routines/audio/audio_set_volume.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;

constexpr uint8_t target_volume = 42;
constexpr uint64_t target_node_id = 1234567;
constexpr bool target_mute_on = false;

class AudioSetVolumeRoutineTest : public testing::Test {
 protected:
  AudioSetVolumeRoutineTest() = default;
  AudioSetVolumeRoutineTest(const AudioSetVolumeRoutineTest&) = delete;
  AudioSetVolumeRoutineTest& operator=(const AudioSetVolumeRoutineTest&) =
      delete;

  void CreateRoutine() {
    routine_ = std::make_unique<AudioSetVolumeRoutine>(
        &mock_context_, target_node_id, target_volume, target_mute_on);
  }

  org::chromium::cras::ControlProxyMock* mock_cras_proxy() {
    return mock_context_.mock_cras_proxy();
  }

  void SetSetOutputUserMute() {
    EXPECT_CALL(*mock_cras_proxy(), SetOutputUserMute(target_mute_on, _, _))
        .WillOnce(Return(true));
  }

  void SetSetOutputUserMuteError() {
    EXPECT_CALL(*mock_cras_proxy(), SetOutputUserMute(target_mute_on, _, _))
        .WillOnce(DoAll(WithArg<1>(Invoke([](brillo::ErrorPtr* error) {
                          *error = brillo::Error::Create(FROM_HERE, "", "", "");
                        })),
                        Return(false)));
  }

  void SetSetOutputNodeVolume() {
    EXPECT_CALL(*mock_cras_proxy(),
                SetOutputNodeVolume(target_node_id, target_volume, _, _))
        .WillOnce(Return(true));
  }

  void SetSetOutputNodeVolumeError() {
    EXPECT_CALL(*mock_cras_proxy(), SetOutputNodeVolume(_, _, _, _))
        .WillOnce(DoAll(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
                          *error = brillo::Error::Create(FROM_HERE, "", "", "");
                        })),
                        Return(false)));
  }

  MockContext mock_context_;
  std::unique_ptr<AudioSetVolumeRoutine> routine_;
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
};

TEST_F(AudioSetVolumeRoutineTest, DefaultConstruction) {
  CreateRoutine();
  EXPECT_EQ(routine_->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);
}

TEST_F(AudioSetVolumeRoutineTest, SuccessfulCase) {
  CreateRoutine();
  SetSetOutputUserMute();
  SetSetOutputNodeVolume();

  routine_->Start();
  EXPECT_EQ(routine_->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kPassed);
}

TEST_F(AudioSetVolumeRoutineTest, SetOutputUserMuteError) {
  CreateRoutine();
  SetSetOutputUserMuteError();

  routine_->Start();
  EXPECT_EQ(routine_->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kError);
}

TEST_F(AudioSetVolumeRoutineTest, SetOutputNodeVolumeError) {
  CreateRoutine();
  SetSetOutputUserMute();
  SetSetOutputNodeVolumeError();

  routine_->Start();
  EXPECT_EQ(routine_->GetStatus(), mojom::DiagnosticRoutineStatusEnum::kError);
}

}  // namespace
}  // namespace diagnostics
