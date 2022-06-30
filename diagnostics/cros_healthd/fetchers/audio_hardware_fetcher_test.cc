// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/audio_hardware_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

constexpr char kAsoundPath[] = "/proc/asound";
constexpr char kFakeAlsaId[] = "FakeId";

class AudioHardwareFetcherTest : public BaseFileTest {
 protected:
  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());
    // Set id so by default we can get valid result.
    SetFile({kAsoundPath, "card0", "id"}, kFakeAlsaId);
  }

  mojom::AudioHardwareResultPtr FetchAudioHardwareInfoSync() {
    base::RunLoop run_loop;
    mojom::AudioHardwareResultPtr result;
    FetchAudioHardwareInfo(
        &mock_context_,
        base::BindLambdaForTesting([&](mojom::AudioHardwareResultPtr response) {
          result = std::move(response);
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

 private:
  base::test::SingleThreadTaskEnvironment env_;
  MockContext mock_context_;
};

TEST_F(AudioHardwareFetcherTest, FetchAudioCardsId) {
  SetFile({kAsoundPath, "card0", "id"}, kFakeAlsaId);

  auto result = FetchAudioHardwareInfoSync();
  EXPECT_EQ(result->get_audio_hardware_info()->audio_cards[0]->alsa_id,
            kFakeAlsaId);
}

TEST_F(AudioHardwareFetcherTest, FetchAudioCardsNoId) {
  UnsetPath({kAsoundPath, "card0", "id"});

  auto result = FetchAudioHardwareInfoSync();
  EXPECT_TRUE(result->is_error());
}

TEST_F(AudioHardwareFetcherTest, FetchAudioCardsCodec) {
  // Test the parser with syntax found in a real HDA codec file.
  SetFile({kAsoundPath, "card0", "codec#2"},
          R"CODEC(Codec: Test Codec Name
Address: 2
Field A: A
  Indended Field B: B
  Attr = Value, Attr = Value
  Field: value: another value
    value
)CODEC");

  auto result = FetchAudioHardwareInfoSync();
  const auto& codec =
      result->get_audio_hardware_info()->audio_cards[0]->hd_audio_codecs[0];
  EXPECT_EQ(codec->name, "Test Codec Name");
  EXPECT_EQ(codec->address, 2);
}

TEST_F(AudioHardwareFetcherTest, FetchAudioCardsCodecNoName) {
  // Missing "Codec:" field.
  SetFile({kAsoundPath, "card0", "codec#0"},
          R"CODEC(Address: 0
)CODEC");

  auto result = FetchAudioHardwareInfoSync();
  EXPECT_TRUE(result->is_error());
}

TEST_F(AudioHardwareFetcherTest, FetchAudioCardsCodecNoAddress) {
  // Missing "Address:" field.
  SetFile({kAsoundPath, "card0", "codec#0"},
          R"CODEC(Codec: Test Codec Name
)CODEC");

  auto result = FetchAudioHardwareInfoSync();
  EXPECT_TRUE(result->is_error());
}

}  // namespace
}  // namespace diagnostics
