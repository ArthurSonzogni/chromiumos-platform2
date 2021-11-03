/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_

#include <memory>

#include <base/time/time.h>
#include "cros-camera/export.h"

namespace cros {

enum class JpegProcessType { kDecode, kEncode };

enum class JpegProcessMethod { kHardware, kSoftware };

enum class FaceAeFunction {
  // Doesn't support ROI control.
  kUnsupported,
  // Supports ROI control, but doesn't enable face AE.
  kNotEnabled,
  // Supports ROI control and enabled face AE.
  kEnabled,
  // Supports ROI control and enabled face AE from app, but forcedly disabled by
  // user.
  kForceDisabled,
  // For SendEnumToUMA() usage.
  kMaxValue = kForceDisabled,
};

enum class HdrnetStreamConfiguration {
  kSingleYuvStream = 0,
  kSingleYuvStreamWithBlob = 1,
  kMultipleYuvStreams = 2,
  kMultipleYuvStreamsWithBlob = 3,
  kMultipleYuvStreamsOfDifferentAspectRatio = 4,
  kMultipleYuvStreamsOfDifferentAspectRatioWithBlob = 5,
  kMaxValue = kMultipleYuvStreamsOfDifferentAspectRatioWithBlob,
};

enum class HdrnetStreamType {
  // HDRnet stream for YUV output.
  kYuv,
  // HDRnet stream for BLOB output.
  kBlob
};

enum class HdrnetProcessingType {
  // Pre-processing of input YUV into linear RGB domain.
  kPreprocessing,
  // Main HDRnet inferencing and rendering.
  kRgbPipeline,
  // Post-processing of HDRnet RGB output to final YUV output(s).
  kPostprocessing,
};

enum class HdrnetError {
  kNoError = 0,
  // Error during HDRnet stream manipulator initialization.
  kInitializationError = 1,
  // Error when waiting for buffer acquire fence.
  kSyncWaitError = 2,
  // Error when running HDRnet processor.
  kHdrnetProcessorError = 3,
  // Error in pre-processing input buffer to the HDRnet pipeline.
  kPreprocessingError = 4,
  // Error when running linear RGB pipeline.
  kRgbPipelineError = 5,
  // Error in post-processing the RGB buffer to produce the output buffers.
  kPostprocessingError = 6,
  // Error triggered by camera HAL.
  kCameraHal3Error = 7,
  kMaxValue = kCameraHal3Error,
};

class CROS_CAMERA_EXPORT CameraMetrics {
 public:
  static std::unique_ptr<CameraMetrics> New();

  virtual ~CameraMetrics() = default;

  // Records the process time of JDA/JEA in microseconds.
  virtual void SendJpegProcessLatency(JpegProcessType process_type,
                                      JpegProcessMethod process_layer,
                                      base::TimeDelta latency) = 0;

  // Records the resolution of image that JDA/JEA process in pixels.
  virtual void SendJpegResolution(JpegProcessType process_type,
                                  JpegProcessMethod process_layer,
                                  int width,
                                  int height) = 0;

  // Records the process time of ConfigureStreams().
  virtual void SendConfigureStreamsLatency(base::TimeDelta latency) = 0;

  // Records the resolution of streams that configured.
  virtual void SendConfigureStreamResolution(int width,
                                             int height,
                                             int format) = 0;

  // Records the type of the client that called OpenDevice().
  virtual void SendOpenDeviceClientType(int client_type) = 0;

  // Records the process time of OpenDevice().
  virtual void SendOpenDeviceLatency(base::TimeDelta latency) = 0;

  // Records the error type which triggers Notify().
  virtual void SendError(int error_code) = 0;

  // Records the camera facing of current session.
  virtual void SendCameraFacing(int facing) = 0;

  // Records the duration of the closing session.
  virtual void SendSessionDuration(base::TimeDelta duration) = 0;

  // Records the face AE function.
  virtual void SendFaceAeFunction(FaceAeFunction function) = 0;

  // Records the max number of detected faces in a camera session
  virtual void SendFaceAeMaxDetectedFaces(int number) = 0;

  // *** HDRnet metrics ***

  // Records the stream configuration including the number of streams, the type
  // of streams, and if the streams are of the same aspect ratio.
  virtual void SendHdrnetStreamConfiguration(
      HdrnetStreamConfiguration config) = 0;

  // Records the maximum size (in width * height) of the HDRnet stream
  // configured for |stream_type| output.
  virtual void SendHdrnetMaxStreamSize(HdrnetStreamType stream_type,
                                       int size) = 0;

  // Records the number of concurrent HDRnet streams in a session.
  virtual void SendHdrnetNumConcurrentStreams(int num_streams) = 0;

  // Records the maximum number of output buffers a HDRnet stream produces (> 1
  // means there are multiple streams with the same aspect ratio) in a session.
  virtual void SendHdrnetMaxOutputBuffersRendered(int num_buffers) = 0;

  // Records whether there's an error that can compromise the HDRnet feature,
  // either causing frame drops or stops the pipeline from running completely,
  // in a session.
  virtual void SendHdrnetError(HdrnetError error) = 0;

  // Records the number of HDRnet-rendered still capture shots taken in a
  // session.
  virtual void SendHdrnetNumStillShotsTaken(int num_shots) = 0;

  // Records the average CPU latency in processing |processing_type| in a
  // session.
  virtual void SendHdrnetAvgLatency(HdrnetProcessingType processing_type,
                                    int latency_us) = 0;

  // *** Gcam AE metrics ***

  // Records the average AE convergence latency in frame count per session.
  virtual void SendGcamAeAvgConvergenceLatency(int latency_frames) = 0;

  // Records the average HDR ratio per session.
  virtual void SendGcamAeAvgHdrRatio(int hdr_ratio) = 0;

  // Records the average total exposure time (TET) per session.
  virtual void SendGcamAeAvgTet(int tet) = 0;
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_
