// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cras/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/fetchers/audio_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

using ::chromeos::cros_healthd::mojom::ErrorType;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::WithArg;

namespace {

const brillo::VariantDictionary kInactiveOutputDevice = {
    {cras::kNameProperty, std::string("Inactive output device")},
    {cras::kNodeVolumeProperty, static_cast<uint64_t>(10)},
    {cras::kIsInputProperty, false},
    {cras::kActiveProperty, false}};
const brillo::VariantDictionary kActiveOutputDevice = {
    {cras::kNameProperty, std::string("Active output device")},
    {cras::kNodeVolumeProperty, static_cast<uint64_t>(20)},
    {cras::kIsInputProperty, false},
    {cras::kActiveProperty, true},
    {cras::kNumberOfUnderrunsProperty, static_cast<uint32_t>(13)},
    {cras::kNumberOfSevereUnderrunsProperty, static_cast<uint32_t>(3)}};
const brillo::VariantDictionary kInactiveInputDevice = {
    {cras::kNameProperty, std::string("Inactive input device")},
    {cras::kNodeVolumeProperty, static_cast<uint64_t>(30)},
    {cras::kIsInputProperty, true},
    {cras::kActiveProperty, false}};
const brillo::VariantDictionary kActiveInputDevice = {
    {cras::kNameProperty, std::string("Active input device")},
    {cras::kNodeVolumeProperty, static_cast<uint64_t>(40)},
    {cras::kInputNodeGainProperty, static_cast<uint32_t>(77)},
    {cras::kIsInputProperty, true},
    {cras::kActiveProperty, true}};

struct GetVolumeStateOutput {
  bool output_mute;
  bool input_mute;
  bool output_user_mute;
};

}  // namespace

class AudioFetcherTest : public ::testing::Test {
 protected:
  AudioFetcherTest() = default;

  void SetUp() override { ASSERT_TRUE(mock_context_.Initialize()); }

  AudioFetcher* audio_fetcher() { return &audio_fetcher_; }

  org::chromium::cras::ControlProxyMock* mock_cras_proxy() {
    return mock_context_.mock_cras_proxy();
  }

  std::vector<brillo::VariantDictionary> get_node_infos_output() {
    return get_node_infos_output_;
  }

  void append_node_infos_output(const brillo::VariantDictionary& data) {
    get_node_infos_output_.push_back(data);
  }

 private:
  MockContext mock_context_;
  AudioFetcher audio_fetcher_{&mock_context_};
  std::vector<brillo::VariantDictionary> get_node_infos_output_{
      kInactiveOutputDevice, kInactiveInputDevice};
};

class AudioFetcherGetVolumeStateTest
    : public AudioFetcherTest,
      public testing::WithParamInterface<GetVolumeStateOutput> {
 protected:
  GetVolumeStateOutput params() const { return GetParam(); }
};

// Test that we can fetch all audio metrics correctly.
//
// This is a parameterized test, we test all possible combination of
// GetVolumeState() output.
TEST_P(AudioFetcherGetVolumeStateTest, FetchAudioInfo) {
  // Add active input, output device to the mock output data
  append_node_infos_output(kActiveOutputDevice);
  append_node_infos_output(kActiveInputDevice);
  std::string output_device_name =
      brillo::GetVariantValueOrDefault<std::string>(kActiveOutputDevice,
                                                    cras::kNameProperty);
  std::string input_device_name = brillo::GetVariantValueOrDefault<std::string>(
      kActiveInputDevice, cras::kNameProperty);
  uint64_t output_volume = brillo::GetVariantValueOrDefault<uint64_t>(
      kActiveOutputDevice, cras::kNodeVolumeProperty);
  uint32_t input_gain = brillo::GetVariantValueOrDefault<uint32_t>(
      kActiveInputDevice, cras::kInputNodeGainProperty);
  uint32_t underruns = brillo::GetVariantValueOrDefault<uint32_t>(
      kActiveOutputDevice, cras::kNumberOfUnderrunsProperty);
  uint32_t severe_underruns = brillo::GetVariantValueOrDefault<uint32_t>(
      kActiveOutputDevice, cras::kNumberOfSevereUnderrunsProperty);

  const bool output_mute = params().output_mute;
  const bool input_mute = params().input_mute;
  const bool output_user_mute = params().output_user_mute;

  EXPECT_CALL(*mock_cras_proxy(), GetVolumeState(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(output_mute),
                      SetArgPointee<2>(input_mute),
                      SetArgPointee<3>(output_user_mute), Return(true)));
  EXPECT_CALL(*mock_cras_proxy(), GetNodeInfos(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(get_node_infos_output()), Return(true)));

  auto audio_result = audio_fetcher()->FetchAudioInfo();
  ASSERT_TRUE(audio_result->is_audio_info());

  const auto& audio = audio_result->get_audio_info();
  EXPECT_EQ(output_mute | output_user_mute, audio->output_mute);
  EXPECT_EQ(input_mute, audio->input_mute);
  EXPECT_EQ(output_device_name, audio->output_device_name);
  EXPECT_EQ(output_volume, audio->output_volume);
  EXPECT_EQ(input_device_name, audio->input_device_name);
  EXPECT_EQ(input_gain, audio->input_gain);
  EXPECT_EQ(underruns, audio->underruns);
  EXPECT_EQ(severe_underruns, audio->severe_underruns);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    AudioFetcherGetVolumeStateTest,
    testing::Values(GetVolumeStateOutput{false, false, false},
                    GetVolumeStateOutput{false, false, true},
                    GetVolumeStateOutput{false, true, false},
                    GetVolumeStateOutput{false, true, true},
                    GetVolumeStateOutput{true, false, false},
                    GetVolumeStateOutput{true, false, true},
                    GetVolumeStateOutput{true, true, false},
                    GetVolumeStateOutput{true, true, true}));

// Test no active output device.
TEST_F(AudioFetcherTest, FetchAudioInfoWithoutActiveOutputDevice) {
  EXPECT_CALL(*mock_cras_proxy(), GetVolumeState(_, _, _, _, _, _))
      .WillOnce(DoAll(Return(true)));
  EXPECT_CALL(*mock_cras_proxy(), GetNodeInfos(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(get_node_infos_output()), Return(true)));

  auto audio_result = audio_fetcher()->FetchAudioInfo();
  ASSERT_TRUE(audio_result->is_audio_info());

  const auto& audio = audio_result->get_audio_info();
  EXPECT_EQ("No active output device", audio->output_device_name);
}

// Test that when GetVolumeState fails.
TEST_F(AudioFetcherTest, FetchAudioInfoGetVolumeStateFail) {
  EXPECT_CALL(*mock_cras_proxy(), GetVolumeState(_, _, _, _, _, _))
      .WillOnce(DoAll(WithArg<4>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));

  auto audio_result = audio_fetcher()->FetchAudioInfo();
  ASSERT_TRUE(audio_result->is_error());
  EXPECT_EQ(audio_result->get_error()->type, ErrorType::kSystemUtilityError);
}

// Test that when GetNodeInfos fails.
TEST_F(AudioFetcherTest, FetchAudioInfoGetNodeInfosFail) {
  EXPECT_CALL(*mock_cras_proxy(), GetVolumeState(_, _, _, _, _, _))
      .WillOnce(DoAll(Return(true)));
  EXPECT_CALL(*mock_cras_proxy(), GetNodeInfos(_, _, _))
      .WillOnce(DoAll(WithArg<1>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));

  auto audio_result = audio_fetcher()->FetchAudioInfo();
  ASSERT_TRUE(audio_result->is_error());
  EXPECT_EQ(audio_result->get_error()->type, ErrorType::kSystemUtilityError);
}

}  // namespace diagnostics
