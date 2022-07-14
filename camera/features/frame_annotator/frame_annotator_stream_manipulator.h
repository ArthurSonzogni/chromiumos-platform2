/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <cinttypes>
#include <memory>
#include <vector>

#include <base/callback_forward.h>
#include <skia/gpu/GrDirectContext.h>
#include <skia/core/SkCanvas.h>

#include "cros-camera/camera_thread.h"
#include "gpu/egl/egl_context.h"

namespace cros {

class FrameAnnotatorStreamManipulator : public StreamManipulator {
 public:
  FrameAnnotatorStreamManipulator();
  ~FrameAnnotatorStreamManipulator() override;
  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;

 private:
  using SkCanvasDrawFn = base::RepeatingCallback<void(SkCanvas*)>;

  bool SetUpContextsOnGpuThread();
  bool ProcessCaptureResultOnGpuThread(Camera3CaptureDescriptor* result);
  std::vector<SkCanvasDrawFn> GetPlotters(
      const FeatureMetadata& feature_metadata);
  bool PlotOnGpuThread(camera3_stream_buffer_t* buffer,
                       const std::vector<SkCanvasDrawFn>& plotters);
  void FlushSkSurfaceToBuffer(SkSurface* surface, buffer_handle_t yuv_buf);

  Size active_array_dimension_;
  Size full_frame_size_;
  Rect<uint32_t> full_frame_crop_;
  const camera3_stream_t* yuv_stream_ = nullptr;
  std::unique_ptr<EglContext> egl_context_;
  sk_sp<GrDirectContext> gr_context_;
  CameraThread gpu_thread_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_FRAME_ANNOTATOR_FRAME_ANNOTATOR_STREAM_MANIPULATOR_H_
