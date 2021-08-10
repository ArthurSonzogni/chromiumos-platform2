/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <map>
#include <memory>
#include <queue>
#include <set>
#include <vector>

#include <base/files/scoped_file.h>
#include <camera/camera_metadata.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_thread.h"
#include "features/hdrnet/hdrnet_ae_controller.h"
#include "features/hdrnet/hdrnet_config.h"
#include "features/hdrnet/hdrnet_processor.h"
#include "gpu/shared_image.h"

namespace cros {

class HdrNetStreamManipulator : public StreamManipulator {
 public:
  HdrNetStreamManipulator(
      HdrNetProcessor::Factory hdrnet_processor_factory = base::NullCallback(),
      HdrNetAeController::Factory hdrnet_ae_controller_factory =
          base::NullCallback());

  ~HdrNetStreamManipulator() override;

  // Implementations of StreamManipulator.  These methods are trampolines and
  // all the actual tasks are carried out and sequenced on the |gpu_thread_|
  // with the internal implementations below.
  bool Initialize(const camera_metadata_t* static_info) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;

 private:
  struct HdrNetStreamContext {
    // The original stream requested by the client.
    camera3_stream_t* original_stream;

    // The stream that will be set in place of |original_stream| in capture
    // requests.
    std::unique_ptr<camera3_stream_t> hdrnet_stream;

    // The buffers bound as SharedImage for the |hdrnet_stream|, which will
    // be used in capture requests and for HDRnet processing.
    std::vector<SharedImage> shared_images;

    // The list of available buffers specified as index to |shared_images| that
    // can be set in capture requests when |hdrnet_stream| is set.
    struct UsableBufferInfo {
      int index;
      base::ScopedFD acquire_fence;
    };
    std::queue<UsableBufferInfo> usable_buffer_list;

    // The HDRnet processor instance for this stream.
    std::unique_ptr<HdrNetProcessor> processor;

    // Pops a free buffer from |usable_buffer_list|.
    base::Optional<int> PopBuffer();

    // Pushes a free buffer into |usable_buffer_list|.
    void PushBuffer(int index, base::ScopedFD acquire_fence);
  };

  struct RequestContext {
    // The buffers requested by the client. These buffers will be filled by
    // HDRnet pipeline with the buffers rendered by the pipeline, with
    // downscaling if needed.
    std::vector<camera3_stream_buffer_t> requested_buffers;

    // The buffers that we actually sent to the HAL. For YUV buffers, we request
    // one buffer that can be downscaled to generate all the requested YUV
    // buffers in |requested_buffers|. Non-YUV streams will be included
    // unchanged.
    std::vector<camera3_stream_buffer_t> modified_buffers;

    // The index to the |shared_images| specifying the SharedImage used in
    // |modified_buffers|.
    int buffer_index;
  };

  // Internal implementations of StreamManipulator.  All these methods are
  // sequenced on the |gpu_thread_|.
  bool InitializeOnGpuThread(const camera_metadata_t* static_info);
  bool ConfigureStreamsOnGpuThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnGpuThread(
      Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnGpuThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnGpuThread(Camera3CaptureDescriptor* result);
  bool NotifyOnGpuThread(camera3_notify_msg_t* msg);
  bool FlushOnGpuThread();

  bool SetUpPipelineOnGpuThread();

  void ResetStateOnGpuThread();

  void RecordCaptureMetadataOnGpuThread(Camera3CaptureDescriptor* request);

  void RecordYuvBufferForAeControllerOnGpuThread(int frame_number,
                                                 const SharedImage& yuv_input);

  HdrNetStreamContext* CreateHdrNetStreamContext(camera3_stream_t* requested,
                                                 uint32_t replace_format);
  HdrNetStreamContext* GetHdrNetContextFromRequestedStream(
      camera3_stream_t* requested);
  HdrNetStreamContext* GetHdrNetContextFromHdrNetStream(
      camera3_stream_t* hdrnet);

  CameraThread gpu_thread_;
  HdrNetProcessor::Factory hdrnet_processor_factory_;
  HdrNetConfig config_;
  android::CameraMetadata static_info_;

  std::unique_ptr<EglContext> egl_context_;

  HdrNetAeController::Factory hdrnet_ae_controller_factory_;
  std::unique_ptr<HdrNetAeController> ae_controller_;

  // The mapping between original and replacement buffers for in-flight
  // requests.
  std::map<uint32_t, RequestContext> request_context_;
  std::vector<std::unique_ptr<HdrNetStreamContext>> stream_replace_context_;
  std::map<camera3_stream_t*, HdrNetStreamContext*> request_stream_mapping_;
  std::map<camera3_stream_t*, HdrNetStreamContext*> result_stream_mapping_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_
