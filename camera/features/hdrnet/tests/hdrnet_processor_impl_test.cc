/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_processor_impl.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <camera/camera_metadata.h>
#include <drm_fourcc.h>
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

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_buffer_utils.h"
#include "cros-camera/common.h"
#include "gpu/egl/utils.h"
#include "gpu/gles/utils.h"
#include "gpu/test_support/gl_test_fixture.h"

#if USE_IPU6 || USE_IPU6EP
#include <system/camera_metadata_hidden.h>
#include <system/camera_vendor_tags.h>

#include "features/third_party/intel/intel_vendor_metadata_tags.h"

// Minimal vendor_tag_ops_t implementation just to keep the test running.
vendor_tag_ops_t ipu6ep_vendor_tag_ops = {
    .get_tag_count = [](const vendor_tag_ops_t* v) -> int { return 1; },
    .get_all_tags =
        [](const vendor_tag_ops_t* v, uint32_t* tag_array) {
          ASSERT_NE(tag_array, nullptr);
          tag_array[0] = INTEL_VENDOR_CAMERA_TONE_MAP_CURVE;
        },
    .get_section_name = [](const vendor_tag_ops_t* v,
                           uint32_t tag) -> const char* {
      switch (tag) {
        case INTEL_VENDOR_CAMERA_TONE_MAP_CURVE:
          return "Intel.VendorCamera";
      }
      return nullptr;
    },
    .get_tag_name = [](const vendor_tag_ops_t* v, uint32_t tag) -> const char* {
      switch (tag) {
        case INTEL_VENDOR_CAMERA_TONE_MAP_CURVE:
          return "ToneMapCurve";
      }
      return nullptr;
    },
    .get_tag_type = [](const vendor_tag_ops_t* v, uint32_t tag) -> int {
      switch (tag) {
        case INTEL_VENDOR_CAMERA_TONE_MAP_CURVE:
          return TYPE_FLOAT;
      }
      return -1;
    }};
#endif

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

  int benchmark_iterations = 1000;
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
      CHECK(base::StringToInt(arg, &g_args.benchmark_iterations));
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

class HdrNetProcessorTest : public GlTestFixture {
 public:
  HdrNetProcessorTest() {
    android::CameraMetadata static_info;
    int32_t max_curve_points = 1024;
    static_info.update(ANDROID_TONEMAP_MAX_CURVE_POINTS, &max_curve_points, 1);
    std::unique_ptr<HdrNetProcessorDeviceAdapter> device_processor;
    if (g_args.use_default_processor_device_adapter) {
      device_processor = std::make_unique<HdrNetProcessorDeviceAdapter>();
    } else {
      device_processor = HdrNetProcessorDeviceAdapter::CreateInstance(
          static_info.getAndLock(), base::ThreadTaskRunnerHandle::Get());
    }
    processor_ = std::make_unique<HdrNetProcessorImpl>(
        static_info.getAndLock(), base::ThreadTaskRunnerHandle::Get(),
        std::move(device_processor));

    constexpr uint32_t kBufferUsage =
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    input_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.input_size.width, g_args.input_size.height, g_args.input_format,
        kBufferUsage);
    input_image_ = SharedImage::CreateFromBuffer(
        *input_buffer_.get(), Texture2D::Target::kTarget2D,
        /*separate_yuv_textures=*/true);
    CHECK(input_image_.y_texture().IsValid() &&
          input_image_.uv_texture().IsValid());
    for (const auto& size : g_args.output_sizes) {
      output_buffers_.push_back(CameraBufferManager::AllocateScopedBuffer(
          size.width, size.height, HAL_PIXEL_FORMAT_YCBCR_420_888,
          kBufferUsage));
    }
  }

  ~HdrNetProcessorTest() { processor_ = nullptr; }

  void SetUp() override {
#if USE_IPU6 || USE_IPU6EP
    ASSERT_EQ(set_camera_metadata_vendor_ops(&ipu6ep_vendor_tag_ops), 0)
        << "Cannot set vendor tag ops";
#endif
  }

  void DumpBuffers() {
    if (!g_args.dump_buffer) {
      return;
    }
    const testing::TestInfo* const test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    {
      std::string filename =
          base::StringPrintf("%sInput.bin", test_info->name());
      CHECK(WriteBufferIntoFile(*input_buffer_, base::FilePath(filename)));
    }
    for (const auto& b : output_buffers_) {
      std::string filename =
          base::StringPrintf("%sOutput_%ux%u.bin", test_info->name(),
                             CameraBufferManager::GetWidth(*b),
                             CameraBufferManager::GetHeight(*b));
      CHECK(WriteBufferIntoFile(*b, base::FilePath(filename)));
    }
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_;
  std::unique_ptr<HdrNetProcessorImpl> processor_;
  ScopedBufferHandle input_buffer_;
  SharedImage input_image_;
  std::vector<ScopedBufferHandle> output_buffers_;
};

TEST_F(HdrNetProcessorTest, HdrNetProcessorBenchmark) {
  ASSERT_TRUE(processor_->Initialize(g_args.input_size, g_args.output_sizes));

  android::CameraMetadata result_metadata(/*entryCapacity=*/3,
                                          /*dataCapacity=*/3);
  constexpr int kCurveResolution = 1024;
  std::vector<float> gtm_curve(kCurveResolution * 2);
  // Simple identity curve.
  for (int i = 0; i < kCurveResolution; ++i) {
    int idx = i * 2;
    gtm_curve[idx] = static_cast<float>(i) / kCurveResolution;
#if USE_IPU6 || USE_IPU6EP
    // 1.0 means 1x gain.
    gtm_curve[idx + 1] = 1.0;
#else
    gtm_curve[idx + 1] = kCurveResolution * gtm_curve[idx];
#endif
  }

#if USE_IPU6 || USE_IPU6EP
  ASSERT_EQ(result_metadata.update(INTEL_VENDOR_CAMERA_TONE_MAP_CURVE,
                                   gtm_curve.data(), gtm_curve.size()),
            0)
      << "Cannot set tonemap curve in vendor tag";
#else
  result_metadata.update(ANDROID_TONEMAP_CURVE_RED, gtm_curve.data(),
                         gtm_curve.size());
  result_metadata.update(ANDROID_TONEMAP_CURVE_GREEN, gtm_curve.data(),
                         gtm_curve.size());
  result_metadata.update(ANDROID_TONEMAP_CURVE_BLUE, gtm_curve.data(),
                         gtm_curve.size());
#endif

  result_metadata.sort();
  const camera_metadata_t* result_metadata_ptr = result_metadata.getAndLock();

  if (g_args.input_file) {
    ReadFileIntoBuffer(*input_buffer_, *g_args.input_file);
  } else {
    FillTestPattern(*input_buffer_);
  }

  base::TimeTicks start_ticks = base::TimeTicks::Now();
  base::TimeDelta total_latency_for_updating_gtm_textures;
  base::TimeDelta total_latency_for_processing;

  for (int i = 0; i < g_args.benchmark_iterations; ++i) {
    base::TimeTicks base = base::TimeTicks::Now();
    Camera3CaptureDescriptor result(
        camera3_capture_result_t{.frame_number = static_cast<uint32_t>(i)});
    result.AppendMetadata(result_metadata_ptr);
    processor_->ProcessResultMetadata(&result);
    total_latency_for_updating_gtm_textures += base::TimeTicks::Now() - base;

    std::vector<buffer_handle_t> output_buffers;
    for (const auto& b : output_buffers_) {
      output_buffers.push_back(*b.get());
    }

    base = base::TimeTicks::Now();
    base::ScopedFD fence =
        processor_->Run(i, HdrNetConfig::Options(), input_image_,
                        base::ScopedFD(), output_buffers);
    constexpr int kFenceWaitTimeoutMs = 300;
    ASSERT_EQ(sync_wait(fence.get(), kFenceWaitTimeoutMs), 0);
    total_latency_for_processing += base::TimeTicks::Now() - base;
  }

  uint64_t elapsed_time_us =
      (base::TimeTicks::Now() - start_ticks).InMicroseconds();

  LOGF(INFO) << "Input size: " << g_args.input_size.ToString();
  std::string output_sizes = g_args.output_sizes[0].ToString();
  for (int i = 1; i < g_args.output_sizes.size(); ++i) {
    output_sizes += ", " + g_args.output_sizes[i].ToString();
  }
  LOGF(INFO) << "Output size(s): " << output_sizes;
  LOGF(INFO) << "Number or iterations: " << g_args.benchmark_iterations;
  LOGF(INFO) << "Total elapsed time: " << elapsed_time_us << " (us)";
  LOGF(INFO) << "Avg. processing latency per frame: "
             << elapsed_time_us / g_args.benchmark_iterations << " (us)";
  LOGF(INFO) << "Avg. GTM curve textures update latency per frame: "
             << total_latency_for_updating_gtm_textures.InMicroseconds() /
                    g_args.benchmark_iterations
             << " (us)";
  LOGF(INFO) << "Avg. HDRnet processing latency per frame: "
             << total_latency_for_processing.InMicroseconds() /
                    g_args.benchmark_iterations
             << " (us)";

  processor_->TearDown();
  DumpBuffers();
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
