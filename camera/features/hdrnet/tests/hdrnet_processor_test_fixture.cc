/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/tests/hdrnet_processor_test_fixture.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <camera/camera_metadata.h>
#include <drm_fourcc.h>
#include <hardware/gralloc.h>
#include <sync/sync.h>
#include <system/graphics.h>

#if USE_IPU6 || USE_IPU6EP
#include <system/camera_metadata_hidden.h>
#include <system/camera_vendor_tags.h>

#include "features/third_party/intel/intel_vendor_metadata_tags.h"
#endif  // USE_IPU6 || USE_IPU6EP

#include "cros-camera/camera_buffer_utils.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

#if USE_IPU6 || USE_IPU6EP
// Minimal vendor_tag_ops_t implementation just to keep the test running.
vendor_tag_ops_t ipu6ep_vendor_tag_ops = {
    .get_tag_count = [](const vendor_tag_ops_t* v) -> int { return 1; },
    .get_all_tags =
        [](const vendor_tag_ops_t* v, uint32_t* tag_array) {
          CHECK_NE(tag_array, nullptr);
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
#endif  // USE_IPU6 || USE_IPU6EP

}  // namespace

HdrNetProcessorTestFixture::HdrNetProcessorTestFixture(
    const Size& input_size,
    uint32_t input_hal_pixel_format,
    const std::vector<Size>& output_sizes,
    bool use_default_adapter) {
  // Allocate the input image and populate pixel values with test pattern by
  // default.
  constexpr uint32_t kBufferUsage =
      GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
  input_buffer_ = CameraBufferManager::AllocateScopedBuffer(
      input_size.width, input_size.height, input_hal_pixel_format,
      kBufferUsage);
  input_image_ = SharedImage::CreateFromBuffer(*input_buffer_.get(),
                                               Texture2D::Target::kTarget2D,
                                               /*separate_yuv_textures=*/true);
  CHECK(input_image_.y_texture().IsValid() &&
        input_image_.uv_texture().IsValid());
  FillTestPattern(*input_buffer_);

  // Allocate output buffers for the pipeline.
  for (const auto& size : output_sizes) {
    output_buffers_.push_back(CameraBufferManager::AllocateScopedBuffer(
        size.width, size.height, HAL_PIXEL_FORMAT_YCBCR_420_888, kBufferUsage));
  }

  // Allocate the HDRnet processor instance.
  android::CameraMetadata static_info;
  int32_t max_curve_points = 1024;
  static_info.update(ANDROID_TONEMAP_MAX_CURVE_POINTS, &max_curve_points, 1);
  auto* locked_info = static_info.getAndLock();
  std::unique_ptr<HdrNetProcessorDeviceAdapter> device_adapter;
  if (use_default_adapter) {
    device_adapter = std::make_unique<HdrNetProcessorDeviceAdapter>();
  } else {
    device_adapter = HdrNetProcessorDeviceAdapter::CreateInstance(
        locked_info, base::ThreadTaskRunnerHandle::Get());
  }
  processor_ = std::make_unique<HdrNetProcessorImpl>(
      locked_info, base::ThreadTaskRunnerHandle::Get(),
      std::move(device_adapter));
  static_info.unlock(locked_info);

  // Platform-specific initialization.
#if USE_IPU6 || USE_IPU6EP
  CHECK_EQ(set_camera_metadata_vendor_ops(&ipu6ep_vendor_tag_ops), 0)
      << "Cannot set vendor tag ops";
#endif

  CHECK(processor_->Initialize(input_size, output_sizes));
}

HdrNetProcessorTestFixture::~HdrNetProcessorTestFixture() {
  processor_->TearDown();
}

void HdrNetProcessorTestFixture::LoadInputFile(base::FilePath input_file_path) {
  CHECK(ReadFileIntoBuffer(*input_buffer_, input_file_path));
}

Camera3CaptureDescriptor
HdrNetProcessorTestFixture::ProduceFakeCaptureResult() {
  if (result_metadata_.isEmpty()) {
    result_metadata_ = android::CameraMetadata(/*entryCapacity=*/3,
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
    CHECK_EQ(result_metadata_.update(INTEL_VENDOR_CAMERA_TONE_MAP_CURVE,
                                     gtm_curve.data(), gtm_curve.size()),
             0)
        << "Cannot set tonemap curve in vendor tag";
#else
    result_metadata_.update(ANDROID_TONEMAP_CURVE_RED, gtm_curve.data(),
                            gtm_curve.size());
    result_metadata_.update(ANDROID_TONEMAP_CURVE_GREEN, gtm_curve.data(),
                            gtm_curve.size());
    result_metadata_.update(ANDROID_TONEMAP_CURVE_BLUE, gtm_curve.data(),
                            gtm_curve.size());
#endif
    result_metadata_.sort();
  }

  const camera_metadata_t* result_metadata_ptr = result_metadata_.getAndLock();
  Camera3CaptureDescriptor result(
      camera3_capture_result_t{.frame_number = frame_number_++});
  result.AppendMetadata(result_metadata_ptr);
  result_metadata_.unlock(result_metadata_ptr);
  return result;
}

void HdrNetProcessorTestFixture::DumpBuffers(const char* file_prefix) {
  {
    std::string filename = base::StringPrintf("%sInput.bin", file_prefix);
    CHECK(WriteBufferIntoFile(*input_buffer_, base::FilePath(filename)));
  }
  for (const auto& b : output_buffers_) {
    std::string filename = base::StringPrintf(
        "%sOutput_%ux%u.bin", file_prefix, CameraBufferManager::GetWidth(*b),
        CameraBufferManager::GetHeight(*b));
    CHECK(WriteBufferIntoFile(*b, base::FilePath(filename)));
  }
}

}  // namespace cros
