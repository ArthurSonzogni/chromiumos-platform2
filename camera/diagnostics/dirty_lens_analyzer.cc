// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/dirty_lens_analyzer.h"

#include <base/functional/bind.h>
#include <ml_core/dlc/dlc_ids.h>

#include "cros-camera/common.h"

namespace {

constexpr float kDirtyLensProbabilityThreshold = 0.75;

}  // namespace

namespace cros {

void DirtyLensAnalyzer::Initialize() {
  // base::Unretained(this) is safe because |this| outlives |dlc_client_|.
  dlc_client_ = DlcClient::Create(
      cros::dlc_client::kBlurDetectorDlcId,
      base::BindOnce(&DirtyLensAnalyzer::OnBlurDetectorDlcSuccess,
                     base::Unretained(this)),
      base::BindOnce(&DirtyLensAnalyzer::OnInitializationFailure,
                     base::Unretained(this)));
  if (!dlc_client_) {
    OnInitializationFailure("error creating DlcClient");
    return;
  }
  dlc_client_->InstallDlc();
}

void DirtyLensAnalyzer::OnBlurDetectorDlcSuccess(
    const base::FilePath& dlc_path) {
  dlc_root_path_ = dlc_path;

  base::AutoLock lock(blur_detector_lock_);
  // TODO(imranziad): Load the library in Diagnostics thread.
  blur_detector_ = BlurDetector::Create(dlc_root_path_);
  if (!blur_detector_) {
    OnInitializationFailure("failed to create blur_detector");
    return;
  }

  LOGF(INFO) << "DirtyLensAnalyzer is initialized!";
}

void DirtyLensAnalyzer::OnInitializationFailure(const std::string& error_msg) {
  LOGF(ERROR) << "DirtyLensAnalyzer failed to initialize! error: " << error_msg;
  // TODO(imranziad): Disable dirty lens analyzer.
}

bool DirtyLensAnalyzer::DetectBlurOnNV12(const uint8_t* nv12_data,
                                         const uint32_t height,
                                         const uint32_t width) {
  base::AutoLock lock(blur_detector_lock_);
  if (!blur_detector_) {
    VLOGF(2) << "Blur detector is not available";
    return false;
  }
  float prob = 0.0f;
  if (!blur_detector_->DirtyLensProbabilityFromNV12(nv12_data, height, width,
                                                    &prob)) {
    VLOGF(2) << "Blur detector could not analyze frame: " << width << "x"
             << height;
    return false;
  }
  return prob > kDirtyLensProbabilityThreshold;
}

}  // namespace cros
