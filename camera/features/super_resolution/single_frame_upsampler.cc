// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/super_resolution/single_frame_upsampler.h"

#include <linux/videodev2.h>
#include <sync/sync.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/native_library.h>
#include <base/system/sys_info.h>
#include <cros-camera/libupsample/upsample_wrapper_bindings.h>
#include <cros-camera/libupsample/upsample_wrapper_types.h>
#include <hardware/gralloc.h>
#include <libyuv.h>
#include <libyuv/convert_argb.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kLibraryName[] = "libupsampler.so";
constexpr char kGeraltModelName[] = "GERALT";
constexpr uint32_t kRGBNumOfChannels = 3;
constexpr int kSyncWaitTimeoutMs = 300;

base::NativeLibrary g_library = nullptr;
cros_camera_CreateUpsampleWrapperFn g_create_fn = nullptr;
cros_camera_DeleteUpsampleWrapperFn g_delete_fn = nullptr;
cros_camera_InitUpsamplerFn g_init_upsampler_fn = nullptr;
cros_camera_UpsampleFn g_upsample_fn = nullptr;

bool LoadUpsamplerLibrary(const base::FilePath& dlc_root_path) {
  if (g_library) {
    return true;
  }
  base::FilePath lib_path = dlc_root_path.Append(kLibraryName);
  base::NativeLibraryOptions native_library_options;
  base::NativeLibraryLoadError load_error;
  native_library_options.prefer_own_symbols = true;
  g_library = base::LoadNativeLibraryWithOptions(
      lib_path, native_library_options, &load_error);

  if (!g_library) {
    LOG(ERROR) << "Upsampler library load error: " << load_error.ToString();
    return false;
  }

  LOGF(INFO) << "Loading upsampler library from: " << lib_path;

  g_create_fn = reinterpret_cast<cros_camera_CreateUpsampleWrapperFn>(
      base::GetFunctionPointerFromNativeLibrary(
          g_library, "cros_camera_CreateUpsampleWrapper"));
  g_delete_fn = reinterpret_cast<cros_camera_DeleteUpsampleWrapperFn>(
      base::GetFunctionPointerFromNativeLibrary(
          g_library, "cros_camera_DeleteUpsampleWrapper"));
  g_init_upsampler_fn = reinterpret_cast<cros_camera_InitUpsamplerFn>(
      base::GetFunctionPointerFromNativeLibrary(g_library,
                                                "cros_camera_InitUpsampler"));
  g_upsample_fn = reinterpret_cast<cros_camera_UpsampleFn>(
      base::GetFunctionPointerFromNativeLibrary(g_library,
                                                "cros_camera_Upsample"));

  bool load_ok = (g_create_fn != nullptr) && (g_delete_fn != nullptr) &&
                 (g_init_upsampler_fn != nullptr) && (g_upsample_fn != nullptr);

  if (!load_ok) {
    LOGF(ERROR) << "g_create_fn: " << g_create_fn;
    LOGF(ERROR) << "g_delete_fn: " << g_delete_fn;
    LOGF(ERROR) << "g_init_upsampler_fn: " << g_init_upsampler_fn;
    LOGF(ERROR) << "g_upsample_fn: " << g_upsample_fn;

    LOGF(ERROR) << "Upsampler library cannot load the expected functions";
    g_library = nullptr;
    return false;
  }

  return true;
}

}  // namespace

SingleFrameUpsampler::~SingleFrameUpsampler() {
  if (runner_ && g_delete_fn) {
    g_delete_fn(runner_);
  }
}

bool SingleFrameUpsampler::Initialize(const base::FilePath& dlc_root_path) {
  if (!LoadUpsamplerLibrary(dlc_root_path)) {
    LOGF(ERROR) << "Failed to load upsampler library";
    return false;
  }

  // Create a function pointer of UpsampleWrapper.
  runner_ = g_create_fn();

  // Use NNAPI delegate for APU accelerator. Default to OpenCL for others.
  InferenceMode inference_mode =
      base::SysInfo::HardwareModelName() == kGeraltModelName
          ? cros::InferenceMode::kNnApi
          : cros::InferenceMode::kOpenCL;

  // Initialize the Upsampler engine.
  CHECK(g_init_upsampler_fn);
  if (!g_init_upsampler_fn(runner_, inference_mode,
                           /*use_lancet_alpha=*/true)) {
    LOGF(ERROR) << "Failed to initialize Lancet upsampler engine";
    runner_ = nullptr;
    return false;
  }

  return true;
}

std::optional<base::ScopedFD> SingleFrameUpsampler::ProcessRequest(
    buffer_handle_t input_buffer,
    buffer_handle_t output_buffer,
    base::ScopedFD release_fence) {
  if (!runner_) {
    LOGF(ERROR) << "Upsampler engine is not initialized";
    return std::nullopt;
  }

  if (release_fence.is_valid() &&
      sync_wait(release_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on input buffer";
    return std::nullopt;
  }

  ScopedMapping input_mapping(input_buffer);
  ScopedMapping output_mapping(output_buffer);
  uint32_t input_width = input_mapping.width();
  uint32_t input_height = input_mapping.height();
  uint32_t output_width = output_mapping.width();
  uint32_t output_height = output_mapping.height();
  uint32_t v4l2_format = input_mapping.v4l2_format();
  DCHECK_EQ(output_mapping.v4l2_format(), v4l2_format);

  if (!(output_width >= input_width && output_height >= input_height)) {
    LOGF(ERROR) << "Output dimensions must be larger than input dimensions";
    return std::nullopt;
  }

  // Allocate input and output RGB buffers
  std::vector<uint8_t> input_rgb_buf(input_width * input_height *
                                     kRGBNumOfChannels);
  std::vector<uint8_t> output_rgb_buf(output_width * output_height *
                                      kRGBNumOfChannels);

  uint32_t rgb_input_buf_stride = input_width * kRGBNumOfChannels;
  if (!ConvertNV12ToRGB(input_mapping, input_rgb_buf.data(),
                        rgb_input_buf_stride)) {
    LOGF(ERROR) << "Failed to convert from NV12 to RGB";
    return std::nullopt;
  }

  LOGF(INFO) << "Start upsampling processing";

  UpsampleRequest upsample_request = {
      .input_width = static_cast<int>(input_width),
      .input_height = static_cast<int>(input_height),
      .output_width = static_cast<int>(output_width),
      .output_height = static_cast<int>(output_height),
      .rgb_input_data = input_rgb_buf.data(),
      .rgb_output_data = output_rgb_buf.data(),
  };

  CHECK(g_upsample_fn);
  if (!g_upsample_fn(runner_, &upsample_request)) {
    LOGF(ERROR) << "Failed to upsample frame with Lancet";
    return std::nullopt;
  }

  LOGF(INFO) << "Upsampling processing finished";

  uint32_t rgb_output_buf_stride = output_width * kRGBNumOfChannels;
  if (!ConvertRGBToNV12(output_rgb_buf.data(), rgb_output_buf_stride,
                        output_mapping)) {
    LOGF(ERROR) << "Failed to convert from RGB to NV12";
    return std::nullopt;
  }

  return base::ScopedFD();
}

bool SingleFrameUpsampler::ConvertNV12ToRGB(const ScopedMapping& in_mapping,
                                            uint8_t* rgb_buf_addr,
                                            uint32_t rgb_buf_stride) {
  if (in_mapping.v4l2_format() != V4L2_PIX_FMT_NV12) {
    LOGF(ERROR) << "Unsupported format "
                << FormatToString(in_mapping.v4l2_format());
    return false;
  }

  if (libyuv::NV12ToRGB24(in_mapping.plane(0).addr, in_mapping.plane(0).stride,
                          in_mapping.plane(1).addr, in_mapping.plane(1).stride,
                          rgb_buf_addr, rgb_buf_stride, in_mapping.width(),
                          in_mapping.height()) != 0) {
    LOGF(ERROR) << "Failed to convert from NV12 to RGB";
    return false;
  }

  return true;
}

bool SingleFrameUpsampler::ConvertRGBToNV12(const uint8_t* rgb_buf_addr,
                                            uint32_t rgb_buf_stride,
                                            const ScopedMapping& out_mapping) {
  if (out_mapping.v4l2_format() != V4L2_PIX_FMT_NV12) {
    LOGF(ERROR) << "Unsupported format "
                << FormatToString(out_mapping.v4l2_format());
    return false;
  }

  auto div_round_up = [](uint32_t n, uint32_t d) { return ((n + d - 1) / d); };
  uint32_t width = out_mapping.width();
  uint32_t height = out_mapping.height();
  uint32_t ystride = width;
  uint32_t cstride = div_round_up(width, 2);
  uint32_t total_size = width * height + cstride * div_round_up(height, 2) * 2;
  uint32_t uv_plane_size = cstride * div_round_up(height, 2);
  std::vector<uint8_t> i420_y(total_size);
  uint8_t* i420_cb = i420_y.data() + width * height;
  uint8_t* i420_cr = i420_cb + uv_plane_size;

  if (libyuv::RGB24ToI420(rgb_buf_addr, width * kRGBNumOfChannels,
                          i420_y.data(), ystride, i420_cb, cstride, i420_cr,
                          cstride, width, height) != 0) {
    LOGF(ERROR) << "Failed to convert from RGB to I420";
    return false;
  }

  if (libyuv::I420ToNV12(i420_y.data(), ystride, i420_cb, cstride, i420_cr,
                         cstride, out_mapping.plane(0).addr,
                         out_mapping.plane(0).stride, out_mapping.plane(1).addr,
                         out_mapping.plane(1).stride, width, height) != 0) {
    LOGF(ERROR) << "Failed to convert from I420 to NV12";
    return false;
  }

  return true;
}

}  // namespace cros
