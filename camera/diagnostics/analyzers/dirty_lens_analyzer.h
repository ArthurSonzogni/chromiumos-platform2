// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_ANALYZERS_DIRTY_LENS_ANALYZER_H_
#define CAMERA_DIAGNOSTICS_ANALYZERS_DIRTY_LENS_ANALYZER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>

#include "diagnostics/analyzers/frame_analyzer.h"
#include "diagnostics/libs/blur_detector.h"

namespace cros {

class DirtyLensAnalyzer final : public FrameAnalyzer {
 public:
  DirtyLensAnalyzer() = default;
  DirtyLensAnalyzer(const DirtyLensAnalyzer&) = delete;
  DirtyLensAnalyzer& operator=(const DirtyLensAnalyzer&) = delete;

  ~DirtyLensAnalyzer() final = default;

  bool Initialize(const base::FilePath& lib_path);

  void AnalyzeFrame(const camera_diag::mojom::CameraFramePtr& frame) final;

  camera_diag::mojom::AnalyzerResultPtr GetResult() final;

 private:
  bool IsValidFrame(const camera_diag::mojom::CameraFramePtr&);

  bool DetectBlurOnNV12(const camera_diag::mojom::CameraFramePtr& frame);

  const camera_diag::mojom::AnalyzerType type_ =
      camera_diag::mojom::AnalyzerType::kDirtyLens;

  base::Lock blur_detector_lock_;
  std::unique_ptr<BlurDetector> blur_detector_ GUARDED_BY(blur_detector_lock_);

  int analyzed_frames_count_ = 0;
  int dirty_frames_count_ = 0;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_ANALYZERS_DIRTY_LENS_ANALYZER_H_
