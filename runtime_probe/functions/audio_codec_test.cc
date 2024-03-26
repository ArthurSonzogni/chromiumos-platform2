// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/strings/string_util.h>

#include "runtime_probe/functions/audio_codec.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class AudioCodecTest : public BaseFunctionTest {};

TEST_F(AudioCodecTest, ProbeI2cCodecSucceed) {
  SetFile(kAsocPaths[1], "codec1\ncodec2\ncodec3\n");
  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      { "name": "codec1" },
      { "name": "codec2" },
      { "name": "codec3" }
    ]
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());
  EXPECT_EQ(result, ans);
}

TEST_F(AudioCodecTest, ProbeI2cCodecSucceedPreKernel4_4) {
  SetFile(kAsocPaths[0], "codec1\ncodec2\ncodec3\n");
  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      { "name": "codec1" },
      { "name": "codec2" },
      { "name": "codec3" }
    ]
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());
  EXPECT_EQ(result, ans);
}

TEST_F(AudioCodecTest, ProbeI2cCodecIgnoreInvalidCodec) {
  SetFile(kAsocPaths[1], base::JoinString(kKnownInvalidCodecNames, "\n"));
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());
  EXPECT_EQ(result, ans);
}

TEST_F(AudioCodecTest, ProbeHdaCodecSucceed) {
  std::string hda_codec_file_1 = R"(Codec: Codec Name 1
Address: 0
AFG Function Id: 0x1 (unsol 1)
Vendor Id: 0x1111111
No Modem Function Group found
Field A: BBB CCC
  Field B: aaa=0x00, bbb=0x01, ccc=0x02, ddd=3
    Field C = 123, Field D = 456
)";
  std::string hda_codec_file_2 = R"(Field A: Value A
Codec: Codec:Name:2
Field B: Value B
Codec: Codec Name 3
Field C: Value C)";

  SetFile("/proc/asound/card0/codec#0", hda_codec_file_1);
  SetFile("/proc/asound/card1/codec#1", hda_codec_file_2);

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      { "name": "Codec Name 1" },
      { "name": "Codec:Name:2" },
      { "name": "Codec Name 3" }
    ]
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(result, ans);
}

TEST_F(AudioCodecTest, ProbeHdaCodecNonHdaCodecFiles) {
  std::string non_hda_codec_file_1 = R"(Codec: CodecHDMI
Codec: Codec HDMI
Codec: HDMICodec
Codec: HDMI Codec)";
  std::string non_hda_codec_file_2 = "Don't care";

  SetFile("/proc/asound/card0/codec#0", non_hda_codec_file_1);
  SetFile("/proc/asound/card1/codec#1", non_hda_codec_file_2);

  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());
  EXPECT_EQ(result, ans);
}

TEST_F(AudioCodecTest, ProbeI2cAndHdaCodecSucceed) {
  SetFile("/proc/asound/card0/codec#0", "Codec: codec1\nCodec: codec2");
  SetFile("/proc/asound/card1/codec#1", "Codec: codec3");
  SetFile(kAsocPaths[1], "codec4\ncodec5\ncodec6\n");

  auto ans = CreateProbeResultFromJson(R"JSON(
    [
      { "name": "codec1" },
      { "name": "codec2" },
      { "name": "codec3" },
      { "name": "codec4" },
      { "name": "codec5" },
      { "name": "codec6" }
    ]
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());

  ExpectUnorderedListEqual(result, ans);
}

TEST_F(AudioCodecTest, NoCodecFile) {
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");

  auto probe_function = CreateProbeFunction<AudioCodecFunction>();
  auto result = EvalProbeFunction(probe_function.get());
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
