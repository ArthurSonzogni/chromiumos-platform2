/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/rotate_and_crop/rotate_and_crop_stream_manipulator.h"

#include <drm_fourcc.h>
#include <libyuv.h>
#include <sync/sync.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/bits.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>

#include "common/camera_hal3_helpers.h"
#include "common/resizable_cpu_buffer.h"
#include "common/stream_manipulator_helper.h"
#include "common/vendor_tag_manager.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "cros-camera/device_config.h"
#include "gpu/gpu_resources.h"

namespace cros {

namespace {

class RotateAndCropVendorTag {
 public:
  static constexpr char kSectionName[] = "com.google.cros_rotate_and_crop";
  static constexpr char kHalAvailableModesTagName[] = "halAvailableModes";
  static constexpr uint32_t kHalAvailableModes =
      kCrosRotateAndCropVendorTagStart;
};

uint8_t DegreesToRotateAndCropMode(int crop_rotate_scale_degrees) {
  switch (crop_rotate_scale_degrees) {
    case CAMERA3_STREAM_ROTATION_0:
      return ANDROID_SCALER_ROTATE_AND_CROP_NONE;
    case CAMERA3_STREAM_ROTATION_90:
      return ANDROID_SCALER_ROTATE_AND_CROP_90;
    case CAMERA3_STREAM_ROTATION_180:
      return ANDROID_SCALER_ROTATE_AND_CROP_180;
    case CAMERA3_STREAM_ROTATION_270:
      return ANDROID_SCALER_ROTATE_AND_CROP_270;
    default:
      NOTREACHED();
      return ANDROID_SCALER_ROTATE_AND_CROP_NONE;
  }
}

libyuv::RotationMode RotateAndCropModeToLibyuvRotation(uint8_t rc_mode) {
  switch (rc_mode) {
    case ANDROID_SCALER_ROTATE_AND_CROP_NONE:
      return libyuv::kRotate0;
    case ANDROID_SCALER_ROTATE_AND_CROP_90:
      return libyuv::kRotate90;
    case ANDROID_SCALER_ROTATE_AND_CROP_180:
      return libyuv::kRotate180;
    case ANDROID_SCALER_ROTATE_AND_CROP_270:
      return libyuv::kRotate270;
    default:
      NOTREACHED();
      return libyuv::kRotate0;
  }
}

bool NeedRotateAndCropApi(mojom::CameraClientType client_type) {
  // Exclude devices that don't pass CTS until we have proper solutions.
  constexpr const char* kExclusiveBoards[] = {"atlas",    "brya",      "kukui",
                                              "nautilus", "nocturne",  "rex",
                                              "staryu",   "strongbad", "zork"};
  const std::string board = base::SysInfo::GetLsbReleaseBoard();
  for (auto* b : kExclusiveBoards) {
    if (board.find(b) == 0) {
      LOGF(WARNING) << "Disabled for board " << board;
      return false;
    }
  }
  // The camera client is ARC and is T or higher.
  return client_type == mojom::CameraClientType::ANDROID &&
         DeviceConfig::GetArcApiLevel() >= 33;
}

}  // namespace

RotateAndCropStreamManipulator::RotateAndCropStreamManipulator(
    GpuResources* gpu_resources,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    std::string camera_module_name,
    mojom::CameraClientType camera_client_type)
    : gpu_resources_(gpu_resources),
      still_capture_processor_(std::move(still_capture_processor)),
      camera_module_name_(std::move(camera_module_name)),
      camera_client_type_(camera_client_type),
      thread_("RotateAndCropThread") {
  CHECK_NE(gpu_resources_, nullptr);
  CHECK(thread_.Start());
}

RotateAndCropStreamManipulator::~RotateAndCropStreamManipulator() {
  thread_.Stop();
}

// static
bool RotateAndCropStreamManipulator::UpdateVendorTags(
    VendorTagManager& vendor_tag_manager) {
  if (!vendor_tag_manager.Add(RotateAndCropVendorTag::kHalAvailableModes,
                              RotateAndCropVendorTag::kSectionName,
                              RotateAndCropVendorTag::kHalAvailableModesTagName,
                              TYPE_BYTE)) {
    LOGF(ERROR) << "Failed to add vendor tag";
    return false;
  }
  return true;
}

// static
bool RotateAndCropStreamManipulator::UpdateStaticMetadata(
    android::CameraMetadata* static_info, mojom::CameraClientType client_type) {
  if (!NeedRotateAndCropApi(client_type)) {
    return true;
  }
  camera_metadata_entry_t entry =
      static_info->find(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES);
  if (entry.count > 0) {
    CHECK_EQ(entry.type, TYPE_BYTE);
    const std::vector<uint8_t> modes(entry.data.u8,
                                     entry.data.u8 + entry.count);
    if (static_info->update(RotateAndCropVendorTag::kHalAvailableModes,
                            modes.data(), modes.size()) != 0) {
      LOGF(ERROR) << "Failed to update "
                  << RotateAndCropVendorTag::kHalAvailableModesTagName;
      return false;
    }
  }
  constexpr uint8_t kClientAvailableRotateAndCropModes[] = {
      ANDROID_SCALER_ROTATE_AND_CROP_NONE, ANDROID_SCALER_ROTATE_AND_CROP_90,
      ANDROID_SCALER_ROTATE_AND_CROP_180,  ANDROID_SCALER_ROTATE_AND_CROP_270,
      ANDROID_SCALER_ROTATE_AND_CROP_AUTO,
  };
  if (static_info->update(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES,
                          kClientAvailableRotateAndCropModes,
                          std::size(kClientAvailableRotateAndCropModes)) != 0) {
    LOGF(ERROR)
        << "Failed to update ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES";
    return false;
  }
  if (!AddListItemToMetadataTag(
          static_info, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
          ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES)) {
    LOGF(ERROR)
        << "Failed to update ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS";
    return false;
  }
  if (!AddListItemToMetadataTag(static_info,
                                ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                                ANDROID_SCALER_ROTATE_AND_CROP)) {
    LOGF(ERROR) << "Failed to update ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS";
    return false;
  }
  if (!AddListItemToMetadataTag(static_info,
                                ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                                ANDROID_SCALER_ROTATE_AND_CROP)) {
    LOGF(ERROR) << "Failed to update ANDROID_REQUEST_AVAILABLE_RESULT_KEYS";
    return false;
  }

  return true;
}

bool RotateAndCropStreamManipulator::Initialize(
    const camera_metadata_t* static_info, Callbacks callbacks) {
  disabled_ = !NeedRotateAndCropApi(camera_client_type_);
  helper_ = std::make_unique<StreamManipulatorHelper>(
      StreamManipulatorHelper::Config{
          .process_mode = disabled_ ? ProcessMode::kBypass
                                    : ProcessMode::kVideoAndStillProcess,
      },
      camera_module_name_, static_info, std::move(callbacks),
      base::BindRepeating(&RotateAndCropStreamManipulator::OnProcessTask,
                          base::Unretained(this)),
      GetCropScaleImageCallback(gpu_resources_->gpu_task_runner(),
                                gpu_resources_->image_processor()),
      std::move(still_capture_processor_), thread_.task_runner());

  base::span<const uint8_t> modes = GetRoMetadataAsSpan<uint8_t>(
      static_info, RotateAndCropVendorTag::kHalAvailableModes);
  hal_available_rc_modes_ = base::flat_set<uint8_t>(modes.begin(), modes.end());
  if (VLOG_IS_ON(1)) {
    std::vector<std::string> mode_strs;
    std::transform(
        hal_available_rc_modes_.begin(), hal_available_rc_modes_.end(),
        std::back_inserter(mode_strs),
        [](uint8_t x) { return std::to_string(base::strict_cast<int>(x)); });
    VLOGF(1) << "HAL available rotate-and-crop modes: ["
             << base::JoinString(mode_strs, ", ") << "]";
  }
  return true;
}

bool RotateAndCropStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  if (!disabled_) {
    CHECK_GT(stream_config->num_streams(), 0);
    client_crs_degrees_ =
        stream_config->GetStreams()[0]->crop_rotate_scale_degrees;
    // Translate |crop_rotate_scale_degrees| to ROTATE_AND_CROP API if the HAL
    // has migrated to it.
    const int hal_crs_degrees = hal_available_rc_modes_.empty()
                                    ? client_crs_degrees_
                                    : CAMERA3_STREAM_ROTATION_0;
    for (auto* stream : stream_config->GetStreams()) {
      stream->crop_rotate_scale_degrees = hal_crs_degrees;
    }

    thread_.PostTaskAsync(
        FROM_HERE,
        base::BindOnce(&RotateAndCropStreamManipulator::ResetBuffersOnThread,
                       base::Unretained(this)));
  }

  return helper_->PreConfigure(stream_config);
}

bool RotateAndCropStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  helper_->PostConfigure(stream_config);
  return true;
}

bool RotateAndCropStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  if (!disabled_ && !helper_->stream_config_unsupported() &&
      !default_request_settings->isEmpty()) {
    const uint8_t rc_mode = ANDROID_SCALER_ROTATE_AND_CROP_AUTO;
    if (default_request_settings->update(ANDROID_SCALER_ROTATE_AND_CROP,
                                         &rc_mode, 1) != 0) {
      LOGF(ERROR) << "Failed to update ANDROID_SCALER_ROTATE_AND_CROP to "
                     "default request";
      return false;
    }
  }
  return true;
}

bool RotateAndCropStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (disabled_ || helper_->stream_config_unsupported()) {
    helper_->HandleRequest(request, true, nullptr);
    return true;
  }

  uint8_t rc_mode_from_crs_degrees =
      DegreesToRotateAndCropMode(client_crs_degrees_);
  auto ctx = std::make_unique<CaptureContext>();
  ctx->client_rc_mode = rc_mode_from_crs_degrees;
  ctx->hal_rc_mode = rc_mode_from_crs_degrees;

  // Check if the client uses ROTATE_AND_CROP API.
  base::span<const uint8_t> rc_mode =
      request->GetMetadata<uint8_t>(ANDROID_SCALER_ROTATE_AND_CROP);
  if (!rc_mode.empty() && rc_mode[0] != ANDROID_SCALER_ROTATE_AND_CROP_AUTO) {
    ctx->client_rc_mode = rc_mode[0];
    ctx->hal_rc_mode = ANDROID_SCALER_ROTATE_AND_CROP_NONE;
  }

  // Check if the HAL has migrated to ROTATE_AND_CROP API and supports the
  // client requested rotation.
  if (!hal_available_rc_modes_.empty()) {
    ctx->hal_rc_mode = hal_available_rc_modes_.contains(ctx->client_rc_mode)
                           ? ctx->client_rc_mode
                           : ANDROID_SCALER_ROTATE_AND_CROP_NONE;
  }

  if (!request->UpdateMetadata<uint8_t>(
          ANDROID_SCALER_ROTATE_AND_CROP,
          std::array<uint8_t, 1>{ctx->hal_rc_mode})) {
    LOGF(ERROR) << "Failed to update ANDROID_SCALER_ROTATE_AND_CROP in request "
                << request->frame_number();
  }

  // Bypass the request when we don't need to do rotation.
  const bool bypass_process = ctx->client_rc_mode == ctx->hal_rc_mode;
  helper_->HandleRequest(request, bypass_process, std::move(ctx));

  return true;
}

bool RotateAndCropStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  if (auto* ctx =
          helper_->GetPrivateContextAs<CaptureContext>(result.frame_number())) {
    if (!ctx->result_metadata_updated &&
        (result.HasMetadata(ANDROID_SCALER_ROTATE_AND_CROP) ||
         result.partial_result() == helper_->partial_result_count())) {
      CHECK(result.UpdateMetadata<uint8_t>(
          ANDROID_SCALER_ROTATE_AND_CROP,
          std::array<uint8_t, 1>{ctx->client_rc_mode}));
      ctx->result_metadata_updated = true;
    }
  }
  helper_->HandleResult(std::move(result));
  return true;
}

void RotateAndCropStreamManipulator::Notify(camera3_notify_msg_t msg) {
  helper_->Notify(msg);
}

bool RotateAndCropStreamManipulator::Flush() {
  helper_->Flush();
  return true;
}

void RotateAndCropStreamManipulator::ResetBuffersOnThread() {
  CHECK(thread_.IsCurrentThread());

  buffer1_.Reset();
  buffer2_.Reset();
}

void RotateAndCropStreamManipulator::OnProcessTask(ScopedProcessTask task) {
  CHECK(thread_.IsCurrentThread());

  auto& ctx = *task->GetPrivateContextAs<CaptureContext>();
  CHECK_EQ(ctx.hal_rc_mode, ANDROID_SCALER_ROTATE_AND_CROP_NONE);
  CHECK_NE(ctx.client_rc_mode, ANDROID_SCALER_ROTATE_AND_CROP_NONE);

  constexpr int kSyncWaitTimeoutMs = 300;
  base::ScopedFD input_release_fence = task->TakeInputReleaseFence();
  if (input_release_fence.is_valid() &&
      sync_wait(input_release_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "Sync wait timed out on input frame "
                << task->frame_number();
    task->Fail();
    return;
  }
  base::ScopedFD output_acquire_fence = task->TakeOutputAcquireFence();
  if (output_acquire_fence.is_valid() &&
      sync_wait(output_acquire_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "Sync wait timed out on output frame "
                << task->frame_number();
    task->Fail();
    return;
  }

  // TODO(kamesan): Offload the rotation to GPU.
  ScopedMapping input_mapping(task->input_buffer());
  ScopedMapping output_mapping(task->output_buffer());
  CHECK_EQ(input_mapping.drm_format(), DRM_FORMAT_NV12);
  CHECK_EQ(output_mapping.drm_format(), DRM_FORMAT_NV12);
  CHECK_EQ(input_mapping.width(), output_mapping.width());
  CHECK_EQ(input_mapping.height(), output_mapping.height());
  CHECK_GT(input_mapping.width(), input_mapping.height());

  uint32_t src_width = input_mapping.width();
  uint32_t src_height = input_mapping.height();
  uint32_t src_offset = 0;
  uint32_t dst_width = src_width;
  uint32_t dst_height = src_height;
  if (ctx.client_rc_mode == ANDROID_SCALER_ROTATE_AND_CROP_90 ||
      ctx.client_rc_mode == ANDROID_SCALER_ROTATE_AND_CROP_270) {
    src_width = base::bits::AlignUp(
        input_mapping.height() * input_mapping.height() / input_mapping.width(),
        2u);
    src_height = input_mapping.height();
    src_offset =
        base::bits::AlignDown((input_mapping.width() - src_width) / 2, 2u);
    dst_width = src_height;
    dst_height = src_width;
  }
  buffer1_.SetFormat(dst_width, dst_height, DRM_FORMAT_YUV420);
  int ret = libyuv::NV12ToI420Rotate(
      input_mapping.plane(0).addr + src_offset, input_mapping.plane(0).stride,
      input_mapping.plane(1).addr + src_offset, input_mapping.plane(1).stride,
      buffer1_.plane(0).addr, buffer1_.plane(0).stride, buffer1_.plane(1).addr,
      buffer1_.plane(1).stride, buffer1_.plane(2).addr,
      buffer1_.plane(2).stride, src_width, src_height,
      RotateAndCropModeToLibyuvRotation(ctx.client_rc_mode));
  if (ret != 0) {
    LOGF(ERROR) << "libyuv::NV12ToI420Rotate() failed: " << ret;
    task->Fail();
    return;
  }

  ResizableCpuBuffer* final_i420 = &buffer1_;
  if (ctx.client_rc_mode == ANDROID_SCALER_ROTATE_AND_CROP_90 ||
      ctx.client_rc_mode == ANDROID_SCALER_ROTATE_AND_CROP_270) {
    buffer2_.SetFormat(input_mapping.width(), input_mapping.height(),
                       DRM_FORMAT_YUV420);
    ret = libyuv::I420Scale(buffer1_.plane(0).addr, buffer1_.plane(0).stride,
                            buffer1_.plane(1).addr, buffer1_.plane(1).stride,
                            buffer1_.plane(2).addr, buffer1_.plane(2).stride,
                            dst_width, dst_height, buffer2_.plane(0).addr,
                            buffer2_.plane(0).stride, buffer2_.plane(1).addr,
                            buffer2_.plane(1).stride, buffer2_.plane(2).addr,
                            buffer2_.plane(2).stride, input_mapping.width(),
                            input_mapping.height(), libyuv::kFilterBilinear);
    if (ret != 0) {
      LOGF(ERROR) << "libyuv::I420Scale() failed: " << ret;
      task->Fail();
      return;
    }
    final_i420 = &buffer2_;
  }

  ret = libyuv::I420ToNV12(
      final_i420->plane(0).addr, final_i420->plane(0).stride,
      final_i420->plane(1).addr, final_i420->plane(1).stride,
      final_i420->plane(2).addr, final_i420->plane(2).stride,
      output_mapping.plane(0).addr, output_mapping.plane(0).stride,
      output_mapping.plane(1).addr, output_mapping.plane(1).stride,
      output_mapping.width(), output_mapping.height());
  if (ret != 0) {
    LOGF(ERROR) << "libyuv::I420ToNV12() failed: " << ret;
    task->Fail();
    return;
  }
}

}  // namespace cros
