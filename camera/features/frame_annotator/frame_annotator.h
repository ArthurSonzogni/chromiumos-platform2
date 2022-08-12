/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_H_
#define CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_H_

#include <optional>

#include <base/callback_forward.h>
#include <hardware/camera3.h>
#include <skia/core/SkCanvas.h>

#include "common/camera_hal3_helpers.h"

namespace cros {

// Interface class that can be used to plot information on frame. The interface
// is a subset of standard camera HAL3, so different usages can collect its own
// information through the API.
class FrameAnnotator {
 public:
  virtual ~FrameAnnotator() = default;

  // A hook to the camera3_device_ops::initialize(). Will be called by
  // FrameAnnotatorStreamManipulator with the camera device static metadata
  // |static_info|.
  virtual bool Initialize(const camera_metadata_t* static_info) = 0;

  // A hook to the camera3_callback_ops::process_capture_result(). Will be
  // called by FrameAnnotatorStreamManipulator for each capture result |result|
  // produced by the camera HAL implementation. This function should only be
  // used for collecting information. Any implementations of this function
  // should not modify the result.
  virtual bool ProcessCaptureResult(const Camera3CaptureDescriptor* result) = 0;

  // Returns true if the frame annotator wants to plot the frame. This function
  // would suggest the FrameAnnotatorStreamManipulator do further optimizations
  // if no plot needed.
  virtual bool IsPlotNeeded() const = 0;

  // A function to plot the frame with Skia's canvas API. Will be called once by
  // FrameAnnotatorStreamManipulator for ecahc yuv frame.
  virtual bool Plot(SkCanvas* canvas) = 0;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_H_
