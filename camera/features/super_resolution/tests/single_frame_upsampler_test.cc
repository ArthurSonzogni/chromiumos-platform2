// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libyuv.h>
#include <linux/videodev2.h>

#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <brillo/flag_helper.h>
#include <gtest/gtest.h>

#include "common/test_support/test_image.h"
#include "cros-camera/common.h"
#include "cros-camera/libupsample/upsample_wrapper_types.h"
#include "features/super_resolution/single_frame_upsampler.h"
#include "ml_core/dlc/dlc_ids.h"
#include "ml_core/dlc/dlc_loader.h"

namespace cros::tests {

namespace {

inline int DivideRoundUp(int dividend, int divisor) {
  CHECK_GT(divisor, 0);
  return (dividend + divisor - 1) / divisor;
}

}  // namespace

base::FilePath g_dlc_path;
base::FilePath g_input_image_path;
base::FilePath g_golden_image_path;

const double kSsimThreshold = 0.8;

class SingleFrameUpsamplerTest : public ::testing::Test {
 protected:
  bool InitializeUpsampler() {
    upsampler_ = std::make_unique<SingleFrameUpsampler>();
    if (!upsampler_->Initialize(g_dlc_path)) {
      LOGF(ERROR) << "Failed to initialize SingleFrameUpsampler";
      return false;
    }
    return true;
  }

  bool LoadTestImages(const base::FilePath& input_image_path,
                      const base::FilePath& golden_image_path) {
    input_image_ = TestImage::Create(input_image_path);
    if (!input_image_) {
      LOGF(ERROR) << "Failed to load input image from " << input_image_path;
      return false;
    }
    golden_image_ = TestImage::Create(golden_image_path);
    if (!golden_image_) {
      LOGF(ERROR) << "Failed to load golden image from " << golden_image_path;
      return false;
    }
    return true;
  }

  bool UpsampleAndCheckSimilarity() {
    // Allocate buffers for the input image, upsampled output, and golden
    // reference image.
    // - `input_buffer`: Stores the input image data.
    // - `output_buffer`: Stores the upsampled image data, which has the same
    // resolution as the golden reference image.
    // - `golden_buffer`: Stores the golden reference image data for comparison.
    ScopedBufferHandle input_buffer = CameraBufferManager::AllocateScopedBuffer(
        input_image_->width(), input_image_->height(),
        HAL_PIXEL_FORMAT_YCbCr_420_888, 0);
    ScopedBufferHandle output_buffer =
        CameraBufferManager::AllocateScopedBuffer(
            golden_image_->width(), golden_image_->height(),
            HAL_PIXEL_FORMAT_YCbCr_420_888, 0);
    ScopedBufferHandle golden_buffer =
        CameraBufferManager::AllocateScopedBuffer(
            golden_image_->width(), golden_image_->height(),
            HAL_PIXEL_FORMAT_YCbCr_420_888, 0);

    if (!WriteTestImageToBuffer(*input_image_, *input_buffer)) {
      LOGF(ERROR) << "Failed to write test image to buffer";
      return false;
    }
    if (!WriteTestImageToBuffer(*golden_image_, *golden_buffer)) {
      LOGF(ERROR) << "Failed to write golden image to buffer";
      return false;
    }

    // Perform upsampling on the input image.
    std::optional<base::ScopedFD> fence = upsampler_->ProcessRequest(
        *input_buffer, *output_buffer, base::ScopedFD(),
        ResamplingMethod::kLancet, /*use_lancet_alpha=*/true);
    if (!fence.has_value()) {
      LOGF(ERROR) << "Failed to upsample from input buffer";
      return false;
    }

    // Compare the upsampled image with the golden reference using SSIM. A
    // higher SSIM indicates greater similarity.
    ScopedMapping output_buffer_mapping(*output_buffer);
    ScopedMapping golden_buffer_mapping(*golden_buffer);
    double ssim = ComputeSsim(output_buffer_mapping, golden_buffer_mapping);
    LOGF(INFO) << "Upsampled image similarity to golden reference: " << ssim;

    // Return true if the SSIM score is greater than the threshold.
    return ssim > kSsimThreshold;
  }

 private:
  struct YuvImagePlane {
    YuvImagePlane(uint32_t stride, uint32_t size, uint8_t* addr)
        : stride(stride), size(size), addr(addr) {}

    uint32_t stride;
    uint32_t size;
    uint8_t* addr;
  };

  struct YuvImage {
    YuvImage(uint32_t w, uint32_t h) : width(w), height(h) {
      uint32_t cstride = DivideRoundUp(w, 2);
      size = w * h + cstride * DivideRoundUp(h, 2) * 2;
      uint32_t uv_plane_size = cstride * DivideRoundUp(h, 2);
      data.resize(size);
      planes.emplace_back(w, w * h, data.data());  // y
      planes.emplace_back(cstride, uv_plane_size,
                          planes.back().addr + planes.back().size);  // u
      planes.emplace_back(cstride, uv_plane_size,
                          planes.back().addr + planes.back().size);  // v
    }

    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> data;
    uint32_t size;
    std::vector<YuvImagePlane> planes;
  };

  using ScopedYuvImage = std::unique_ptr<YuvImage>;

  int CovertNV12toI420(const ScopedMapping& in_nv12,
                       const ScopedYuvImage& out_yuv) {
    CHECK_EQ(in_nv12.v4l2_format(), V4L2_PIX_FMT_NV12);
    return libyuv::NV12ToI420(
        in_nv12.plane(0).addr, in_nv12.plane(0).stride, in_nv12.plane(1).addr,
        in_nv12.plane(1).stride, out_yuv->planes[0].addr,
        out_yuv->planes[0].stride, out_yuv->planes[1].addr,
        out_yuv->planes[1].stride, out_yuv->planes[2].addr,
        out_yuv->planes[2].stride, in_nv12.width(), in_nv12.height());
  }

  double ComputeSsim(const ScopedMapping& nv12_result_mapping,
                     const ScopedMapping& nv12_golden_mapping) {
    CHECK_EQ(nv12_result_mapping.width(), nv12_golden_mapping.width());
    CHECK_EQ(nv12_result_mapping.height(), nv12_golden_mapping.height());
    ScopedYuvImage i420_result_image = std::make_unique<YuvImage>(
        nv12_result_mapping.width(), nv12_result_mapping.height());
    ScopedYuvImage i420_golden_image = std::make_unique<YuvImage>(
        nv12_golden_mapping.width(), nv12_golden_mapping.height());
    if (CovertNV12toI420(nv12_result_mapping, i420_result_image) != 0) {
      LOGF(ERROR) << "Fail to convert result image from NV12 to I420";
      return 0.0;
    }
    if (CovertNV12toI420(nv12_golden_mapping, i420_golden_image) != 0) {
      LOGF(ERROR) << "Fail to convert golden image from NV12 to I420";
      return 0.0;
    }
    return libyuv::I420Ssim(
        i420_result_image->planes[0].addr, i420_result_image->planes[0].stride,
        i420_result_image->planes[1].addr, i420_result_image->planes[1].stride,
        i420_result_image->planes[2].addr, i420_result_image->planes[2].stride,
        i420_golden_image->planes[0].addr, i420_golden_image->planes[0].stride,
        i420_golden_image->planes[1].addr, i420_golden_image->planes[1].stride,
        i420_golden_image->planes[2].addr, i420_golden_image->planes[2].stride,
        i420_golden_image->width, i420_golden_image->height);
  }

  std::unique_ptr<SingleFrameUpsampler> upsampler_;
  std::optional<TestImage> input_image_;
  std::optional<TestImage> golden_image_;
};

// Verify the functionality of SingleFrameUpsampler.
TEST_F(SingleFrameUpsamplerTest, TestUpsamplerLibrary) {
  // Initialize the SingleFrameUpsampler. This includes load upsampler library,
  // and set the delegate for inference engine.
  ASSERT_TRUE(InitializeUpsampler());

  // Load one input image for upsampling, and one golden image for SSIM
  // calculation.
  ASSERT_TRUE(LoadTestImages(g_input_image_path, g_golden_image_path));

  // Perform upsampling on the input image and compare the upsampled result
  // with the golden image using SSIM calculation.
  ASSERT_TRUE(UpsampleAndCheckSimilarity());
}

}  // namespace cros::tests

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();

  cros::DlcLoader client(cros::dlc_client::kSuperResDlcId);
  client.Run();
  if (!client.DlcLoaded()) {
    LOG(ERROR) << "Failed to load DLC";
    return -1;
  }
  cros::tests::g_dlc_path = client.GetDlcRootPath();

  // Example command for testing a locally built libupsampler.so:
  // /usr/bin/single_frame_upsampler_test --input_image_path={}
  // --golden_image_path={} --dlc_path=/usr/local/lib64

  DEFINE_string(input_image_path, "", "Input image file path");
  DEFINE_string(golden_image_path, "", "Golden image file path");
  DEFINE_string(dlc_path, "", "DLC path");
  brillo::FlagHelper::Init(argc, argv, "Single Frame Upsampler unit tests");

  LOG_ASSERT(!FLAGS_input_image_path.empty());
  LOG_ASSERT(!FLAGS_golden_image_path.empty());
  if (!FLAGS_dlc_path.empty()) {
    cros::tests::g_dlc_path = base::FilePath(FLAGS_dlc_path);
  }
  cros::tests::g_input_image_path = base::FilePath(FLAGS_input_image_path);
  cros::tests::g_golden_image_path = base::FilePath(FLAGS_golden_image_path);

  return RUN_ALL_TESTS();
}
