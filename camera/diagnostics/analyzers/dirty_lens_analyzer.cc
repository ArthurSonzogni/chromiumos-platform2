// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/analyzers/dirty_lens_analyzer.h"

#include <base/check_op.h>
#include <base/functional/bind.h>
#include <ml_core/dlc/dlc_ids.h>

#include "cros-camera/common.h"

namespace {

// Probability to consider one frame as dirty.
constexpr float kDirtyLensProbabilityThreshold = 0.75f;
// Overall >60% of analyzed frames need to be dirty to consider the lens dirty.
constexpr float kDirtyFramesRatio = 0.60f;
// Minimum no of frames need to be analyzed to provide a result.
constexpr int kMinNoOfAnalyzedFrames = 5;

}  // namespace

namespace cros {

bool DirtyLensAnalyzer::Initialize(const base::FilePath& lib_path) {
  if (lib_path.empty()) {
    VLOGF(1) << "DirtyLensAnalyzer disabled. Library not avaialble.";
    return false;
  }

  base::AutoLock lock(blur_detector_lock_);
  blur_detector_ = BlurDetector::Create(lib_path);
  if (!blur_detector_) {
    VLOGF(1) << "DirtyLensAnalyzer disabled. Failed to create blur detector";
    return false;
  }

  return true;
}

bool DirtyLensAnalyzer::IsValidFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  return (frame->stream->pixel_format ==
              camera_diag::mojom::PixelFormat::kYuv420 &&
          frame->stream->width > 0 && frame->stream->height > 0);
}

void DirtyLensAnalyzer::AnalyzeFrame(
    const camera_diag::mojom::CameraFramePtr& frame) {
  VLOGF(1) << "Running dirty lens analyzer on frame "
           << frame->frame_number.value_or(-1);

  if (!IsValidFrame(frame)) {
    return;
  }

  if (DetectBlurOnNV12(frame)) {
    dirty_frames_count_++;
  }

  analyzed_frames_count_++;
}

camera_diag::mojom::AnalyzerResultPtr DirtyLensAnalyzer::GetResult() {
  LOGF(INFO) << "DirtyLensAnalyzer: total analyzed " << analyzed_frames_count_
             << " frames, dirty " << dirty_frames_count_ << " frames";
  auto result = camera_diag::mojom::AnalyzerResult::New();
  result->type = type_;

  // When fewer than |kMinNoOfAnalyzedFrames| frames are analyzed.
  result->status = camera_diag::mojom::AnalyzerStatus::kNotRun;

  if (analyzed_frames_count_ < kMinNoOfAnalyzedFrames) {
    return result;
  }

  DCHECK_GT(analyzed_frames_count_, 0);
  float ratio = static_cast<float>(dirty_frames_count_) /
                static_cast<float>(analyzed_frames_count_);

  if (ratio > kDirtyFramesRatio) {
    result->status = camera_diag::mojom::AnalyzerStatus::kFailed;
  } else {
    result->status = camera_diag::mojom::AnalyzerStatus::kPassed;
  }

  return result;
}

bool DirtyLensAnalyzer::DetectBlurOnNV12(
    const camera_diag::mojom::CameraFramePtr& frame) {
  int width = frame->stream->width;
  int height = frame->stream->height;
  int nv12_size = (width * height * 3) / 2;

  mojo::ScopedSharedBufferMapping nv12_mapping =
      frame->buffer->shm_handle->Map(nv12_size);

  if (nv12_mapping == nullptr) {
    VLOGF(1) << "Failed to map the diagnostics buffer, frame "
             << frame->frame_number.value_or(-1);
    return false;
  }

  base::AutoLock lock(blur_detector_lock_);
  if (!blur_detector_) {
    VLOGF(1) << "Blur detector is not available";
    return false;
  }
  float prob = 0.0f;
  const uint8_t* nv12_data = static_cast<uint8_t*>(nv12_mapping.get());
  if (!blur_detector_->DirtyLensProbabilityFromNV12(nv12_data, height, width,
                                                    &prob)) {
    VLOGF(1) << "Blur detector could not analyze frame: "
             << frame->frame_number.value_or(-1);
    return false;
  }
  VLOGF(2) << "Blur detection on frame " << frame->frame_number.value_or(-1)
           << ": " << prob;
  return prob > kDirtyLensProbabilityThreshold;
}

}  // namespace cros
