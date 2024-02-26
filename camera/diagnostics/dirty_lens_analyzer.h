// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_DIAGNOSTICS_DIRTY_LENS_ANALYZER_H_
#define CAMERA_DIAGNOSTICS_DIRTY_LENS_ANALYZER_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>
#include <ml_core/dlc/dlc_client.h>

#include "diagnostics/libs/blur_detector.h"

namespace cros {

class DirtyLensAnalyzer {
 public:
  DirtyLensAnalyzer() = default;
  DirtyLensAnalyzer(const DirtyLensAnalyzer&) = delete;
  DirtyLensAnalyzer& operator=(const DirtyLensAnalyzer&) = delete;

  ~DirtyLensAnalyzer() = default;

  void Initialize();
  // Only NV12 frames.
  bool DetectBlurOnNV12(const uint8_t* nv12_data,
                        const uint32_t height,
                        const uint32_t width);

 private:
  void OnBlurDetectorDlcSuccess(const base::FilePath& dlc_path);
  void OnInitializationFailure(const std::string& error_msg);

  base::Lock blur_detector_lock_;
  std::unique_ptr<BlurDetector> blur_detector_ GUARDED_BY(blur_detector_lock_);

  base::FilePath dlc_root_path_;

  // Do not transfer ownership once created. |this| should outlive
  // |dlc_client_|.
  std::unique_ptr<DlcClient> dlc_client_;
};

}  // namespace cros

#endif  // CAMERA_DIAGNOSTICS_DIRTY_LENS_ANALYZER_H_
