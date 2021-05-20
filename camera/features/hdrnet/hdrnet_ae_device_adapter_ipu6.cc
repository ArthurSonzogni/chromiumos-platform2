/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_ae_device_adapter_ipu6.h"

#include <utility>

#include <sync/sync.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/hdrnet/vendor_tags.h"

namespace cros {

namespace {

// IPU6 uses fixed white level of 32000 (for 15-bit value). Scaling the value
// to 8-bit gives us 249.
constexpr int kIpu6WhiteLevel = 249;

}  // namespace

HdrNetAeDeviceAdapterIpu6::HdrNetAeDeviceAdapterIpu6()
    : gcam_ae_(GcamAe::CreateInstance()) {}

bool HdrNetAeDeviceAdapterIpu6::ExtractAeStats(
    int frame_number,
    const camera_metadata_t* result_metadata,
    MetadataLogger* metadata_logger_) {
  base::Optional<int32_t> ae_stats_grid_width = GetRoMetadata<int32_t>(
      result_metadata, CHROMEOS_IPU6_RGBS_STATS_GRID_WIDTH);
  if (!ae_stats_grid_width) {
    VLOGF(2) << "Cannot get CHROMEOS_IPU6_RGBS_STATS_GRID_WIDTH";
    return false;
  }
  base::Optional<int32_t> ae_stats_grid_height = GetRoMetadata<int32_t>(
      result_metadata, CHROMEOS_IPU6_RGBS_STATS_GRID_HEIGHT);
  if (!ae_stats_grid_height) {
    VLOGF(2) << "Cannot get CHROMEOS_IPU6_RGBS_STATS_GRID_HEIGHT";
    return false;
  }
  base::Optional<uint8_t> ae_stats_shading_correction = GetRoMetadata<uint8_t>(
      result_metadata, CHROMEOS_IPU6_RGBS_STATS_SHADING_CORRECTION);
  if (!ae_stats_shading_correction) {
    VLOGF(2) << "Cannot get CHROMEOS_IPU6_RGBS_STATS_SHADING_CORRECTION";
    return false;
  }
  base::span<const uint8_t> ae_stats_blocks = GetRoMetadataAsSpan<uint8_t>(
      result_metadata, CHROMEOS_IPU6_RGBS_STATS_BLOCKS);
  if (ae_stats_blocks.empty()) {
    VLOGF(2) << "Cannot get CHROMEOS_IPU6_RGBS_STATS_BLOCKS";
    return false;
  }

  VLOGF(2) << "ae_stats_grid_width=" << *ae_stats_grid_width;
  VLOGF(2) << "ae_stats_grid_height=" << *ae_stats_grid_height;
  VLOGF(2) << "ae_stats_shading_correction=" << !!*ae_stats_shading_correction;
  VLOGF(2) << "ae_stats_blocks.size()=" << ae_stats_blocks.size();
  if (VLOG_IS_ON(2)) {
    for (int y = 0; y < *ae_stats_grid_height; ++y) {
      for (int x = 0; x < *ae_stats_grid_width; ++x) {
        int base = (y * (*ae_stats_grid_width) + x) * 5;
        int avg_gr = ae_stats_blocks[base];
        int avg_r = ae_stats_blocks[base + 1];
        int avg_b = ae_stats_blocks[base + 2];
        int avg_gb = ae_stats_blocks[base + 3];
        int sat = ae_stats_blocks[base + 4];
        VLOGF(2) << "block (" << x << "," << y
                 << ") sat=" << static_cast<float>(sat) / 255.0
                 << ", avg_gr=" << avg_gr << ", avg_r=" << avg_r
                 << ", avg_b=" << avg_b << ", avg_gb=" << avg_gb;
      }
    }
  }

  // We should create the entry only when there's valid AE stats, so that when
  // HasAeStats() returns true there's indeed valid AE stats.
  base::Optional<AeStatsIntelIpu6*> ae_stats =
      GetAeStatsEntry(frame_number, /*create_entry=*/true);

  (*ae_stats)->white_level = kIpu6WhiteLevel;
  (*ae_stats)->grid_width = *ae_stats_grid_width;
  (*ae_stats)->grid_height = *ae_stats_grid_height;
  int num_grid_blocks = *ae_stats_grid_width * *ae_stats_grid_height;
  for (int i = 0; i < num_grid_blocks; ++i) {
    int base = i * 5;
    AeStatsGridBlockIntelIpu6& block = (*ae_stats)->grid_blocks[i];
    block.avg_gr = ae_stats_blocks[base];
    block.avg_r = ae_stats_blocks[base + 1];
    block.avg_b = ae_stats_blocks[base + 2];
    block.avg_gb = ae_stats_blocks[base + 3];
    block.sat = ae_stats_blocks[base + 4];
  }

  if (metadata_logger_) {
    metadata_logger_->Log(frame_number, kTagWhiteLevel, kIpu6WhiteLevel);
    metadata_logger_->Log(frame_number, kTagIpu6RgbsStatsGridWidth,
                          *ae_stats_grid_width);
    metadata_logger_->Log(frame_number, kTagIpu6RgbsStatsGridHeight,
                          *ae_stats_grid_height);
    metadata_logger_->Log(frame_number, kTagIpu6RgbsStatsShadingCorrection,
                          *ae_stats_shading_correction);
    metadata_logger_->Log(frame_number, kTagIpu6RgbsStatsBlocks,
                          ae_stats_blocks);
  }

  return true;
}

bool HdrNetAeDeviceAdapterIpu6::HasAeStats(int frame_number) {
  return GetAeStatsEntry(frame_number).has_value();
}

AeParameters HdrNetAeDeviceAdapterIpu6::ComputeAeParameters(
    int frame_number, const AeFrameInfo& frame_info, float max_hdr_ratio) {
  AeParameters ae_parameters;
  AeFrameMetadata ae_metadata{
      .actual_analog_gain = frame_info.analog_gain,
      .applied_digital_gain = frame_info.digital_gain,
      .actual_exposure_time_ms = frame_info.exposure_time_ms,
      .sensor_sensitivity = frame_info.estimated_sensor_sensitivity,
      .faces = frame_info.faces,
      .exposure_compensation = frame_info.targeted_ae_compensation,
  };

  VLOGF(1) << "Running Gcam AE "
           << " [" << frame_number << "]"
           << " ae_stats_input="
           << static_cast<int>(frame_info.ae_stats_input_mode)
           << " exposure_time=" << ae_metadata.actual_exposure_time_ms
           << " analog_gain=" << ae_metadata.actual_analog_gain
           << " digital_gain=" << ae_metadata.applied_digital_gain
           << " num_faces=" << ae_metadata.faces.size();

  AeResult ae_result;
  if (frame_info.ae_stats_input_mode == AeStatsInputMode::kFromVendorAeStats) {
    base::Optional<AeStatsIntelIpu6*> ae_stats = GetAeStatsEntry(frame_number);
    if (!ae_stats) {
      LOGF(ERROR) << "Cannot find AE stats entry for frame " << frame_number;
      return ae_parameters;
    }
    AwbInfo awb_info;
    for (int i = 0; i < 4; ++i) {
      awb_info.gains[i] = frame_info.rggb_gains[i];
    }
    for (int i = 0; i < 9; ++i) {
      awb_info.ccm[i] = frame_info.ccm[i];
    }
    ae_result = gcam_ae_->ComputeGcamAe(
        frame_info.active_array_dimension.width,
        frame_info.active_array_dimension.height, ae_metadata, awb_info,
        **ae_stats, max_hdr_ratio);
  } else {  // AeStatsInputMode::kFromYuvImage
    if (!frame_info.HasYuvBuffer()) {
      return ae_parameters;
    }
    if (frame_info.acquire_fence.is_valid() &&
        sync_wait(frame_info.acquire_fence.get(), 300) != 0) {
      LOGF(WARNING) << "sync_wait failed";
      return ae_parameters;
    }

    buffer_handle_t buffer_handle = frame_info.yuv_buffer;
    size_t buffer_width = CameraBufferManager::GetWidth(buffer_handle);
    size_t buffer_height = CameraBufferManager::GetHeight(buffer_handle);
    auto* buf_mgr = CameraBufferManager::GetInstance();
    struct android_ycbcr ycbcr;
    buf_mgr->LockYCbCr(buffer_handle, 0, 0, 0, buffer_width, buffer_height,
                       &ycbcr);
    // NV12 is the only support format at the moment.
    YuvBuffer yuv_buffer;
    yuv_buffer.format = YuvFormat::kNv12;
    yuv_buffer.width = buffer_width;
    yuv_buffer.height = buffer_height;
    // Y plane.
    yuv_buffer.planes[0].width = yuv_buffer.width;
    yuv_buffer.planes[0].height = yuv_buffer.height;
    yuv_buffer.planes[0].stride =
        CameraBufferManager::GetPlaneStride(buffer_handle, 0);
    yuv_buffer.planes[0].data = reinterpret_cast<uint8_t*>(ycbcr.y);
    // UV plane.
    yuv_buffer.planes[1].width = yuv_buffer.width / 2;
    yuv_buffer.planes[1].height = yuv_buffer.height / 2;
    yuv_buffer.planes[1].stride =
        CameraBufferManager::GetPlaneStride(buffer_handle, 1);
    yuv_buffer.planes[1].data = reinterpret_cast<uint8_t*>(ycbcr.cb);

    ae_result = gcam_ae_->ComputeLinearizedGcamAe(
        ae_metadata, std::move(yuv_buffer), max_hdr_ratio);

    buf_mgr->Unlock(buffer_handle);
  }

  ae_parameters.short_tet = ae_result.short_tet;
  ae_parameters.long_tet = ae_result.long_tet;
  return ae_parameters;
}

base::Optional<AeStatsIntelIpu6*> HdrNetAeDeviceAdapterIpu6::GetAeStatsEntry(
    int frame_number, bool create_entry) {
  int index = frame_number % ae_stats_.size();
  AeStatsEntry& entry = ae_stats_[index];
  if (entry.frame_number != frame_number) {
    if (!create_entry) {
      return base::nullopt;
    }
    // Clear the outdated AE stats.
    entry.frame_number = frame_number;
    entry.ae_stats = {};
  }
  return &entry.ae_stats;
}

}  // namespace cros
