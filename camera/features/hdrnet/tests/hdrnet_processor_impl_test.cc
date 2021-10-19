/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <string>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/test/test_timeouts.h>
#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool

// gtest's internal typedef of None and Bool conflicts with the None and Bool
// macros in X11/X.h (https://github.com/google/googletest/issues/371).
// X11/X.h is pulled in by the GL headers we include.
#include <gtest/gtest.h>

#pragma pop_macro("None")
#pragma pop_macro("Bool")
#include <hardware/gralloc.h>
#include <sync/sync.h>
#include <system/graphics.h>

#include "cros-camera/common.h"
#include "features/hdrnet/tests/hdrnet_processor_test_fixture.h"

namespace cros {

struct Options {
  static constexpr const char kBenchmarkIterationsSwitch[] = "iterations";
  static constexpr const char kInputSizeSwitch[] = "input-size";
  static constexpr const char kOutputSizeSwitch[] = "output-sizes";
  static constexpr const char kDumpBufferSwitch[] = "dump-buffer";
  static constexpr const char kInputFile[] = "input-file";
  static constexpr const char kInputFormat[] = "input-format";
  // Use the default device processor to measure the latency of the core HDRnet
  // linear RGB pipeline.
  static constexpr const char kUseDefaulProcessorDeviceAdapter[] =
      "use-default-processor-device-adapter";

  int iterations = 1000;
  Size input_size{1920, 1080};
  std::vector<Size> output_sizes{{1920, 1080}, {1280, 720}};
  bool dump_buffer = false;
  base::Optional<base::FilePath> input_file;
  bool use_default_processor_device_adapter = false;
  uint32_t input_format = HAL_PIXEL_FORMAT_YCbCr_420_888;
};

Options g_args;

void ParseCommandLine(int argc, char** argv) {
  base::CommandLine command_line(argc, argv);
  {
    std::string arg =
        command_line.GetSwitchValueASCII(Options::kBenchmarkIterationsSwitch);
    if (!arg.empty()) {
      CHECK(base::StringToInt(arg, &g_args.iterations));
    }
  }
  {
    std::string arg =
        command_line.GetSwitchValueASCII(Options::kInputSizeSwitch);
    if (!arg.empty()) {
      std::vector<std::string> arg_split = base::SplitString(
          arg, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      CHECK_EQ(arg_split.size(), 2);
      CHECK(base::StringToUint(arg_split[0], &g_args.input_size.width));
      CHECK(base::StringToUint(arg_split[1], &g_args.input_size.height));
    }
  }
  {
    std::string arg =
        command_line.GetSwitchValueASCII(Options::kOutputSizeSwitch);
    if (!arg.empty()) {
      g_args.output_sizes.clear();
      for (auto size : base::SplitString(arg, ",", base::TRIM_WHITESPACE,
                                         base::SPLIT_WANT_NONEMPTY)) {
        std::vector<std::string> arg_split = base::SplitString(
            size, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        CHECK_EQ(arg_split.size(), 2);
        uint32_t width, height;
        CHECK(base::StringToUint(arg_split[0], &width));
        CHECK(base::StringToUint(arg_split[1], &height));
        g_args.output_sizes.push_back({width, height});
      }
    }
  }
  if (command_line.HasSwitch(Options::kDumpBufferSwitch)) {
    g_args.dump_buffer = true;
  }
  {
    std::string arg = command_line.GetSwitchValueASCII(Options::kInputFile);
    if (!arg.empty()) {
      base::FilePath path(arg);
      CHECK(base::PathExists(path)) << ": Input file does not exist";
      g_args.input_file = path;
    }
  }
  {
    std::string arg = command_line.GetSwitchValueASCII(Options::kInputFormat);
    if (!arg.empty()) {
      CHECK(arg == "nv12" || arg == "p010")
          << "Unrecognized input format: " << arg;
      if (arg == "nv12") {
        g_args.input_format = HAL_PIXEL_FORMAT_YCBCR_420_888;
      } else {  // arg == "p010"
        g_args.input_format = HAL_PIXEL_FORMAT_YCBCR_P010;
      }
    }
  }
  if (command_line.HasSwitch(Options::kUseDefaulProcessorDeviceAdapter)) {
    g_args.use_default_processor_device_adapter = true;
  }
}

class HdrNetProcessorTest : public testing::Test {
 public:
  HdrNetProcessorTest()
      : fixture_(g_args.input_size,
                 g_args.input_format,
                 g_args.output_sizes,
                 g_args.use_default_processor_device_adapter) {}
  ~HdrNetProcessorTest() = default;

 protected:
  HdrNetProcessorTestFixture fixture_;
};

TEST_F(HdrNetProcessorTest, FullPipelineTest) {
  if (g_args.input_file) {
    fixture_.LoadInputFile(*g_args.input_file);
  }
  for (int i = 0; i < g_args.iterations; ++i) {
    Camera3CaptureDescriptor result = fixture_.ProduceFakeCaptureResult();
    fixture_.processor()->ProcessResultMetadata(&result);
    base::ScopedFD fence = fixture_.processor()->Run(
        i, HdrNetConfig::Options(), fixture_.input_image(), base::ScopedFD(),
        fixture_.output_buffers());
    constexpr int kFenceWaitTimeoutMs = 300;
    ASSERT_EQ(sync_wait(fence.get(), kFenceWaitTimeoutMs), 0);
  }
  if (g_args.dump_buffer) {
    fixture_.DumpBuffers(testing::UnitTest::GetInstance()
                             ->current_test_info()
                             ->test_case_name());
  }
}

}  // namespace cros

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();
  cros::ParseCommandLine(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
