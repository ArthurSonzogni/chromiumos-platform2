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

#include "common/reloadable_config_file.h"
#include "common/still_capture_processor.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_thread.h"
#include "features/hdrnet/hdrnet_config.h"
#include "features/hdrnet/hdrnet_metrics.h"
#include "features/hdrnet/hdrnet_processor.h"
#include "gpu/shared_image.h"

namespace cros {

class HdrNetStreamManipulator : public StreamManipulator {
 public:
  HdrNetStreamManipulator(
      base::FilePath config_file_path,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor,
      HdrNetProcessor::Factory hdrnet_processor_factory = base::NullCallback());

  ~HdrNetStreamManipulator() override;

  // Implementations of StreamManipulator.  These methods are trampolines and
  // all the actual tasks are carried out and sequenced on the |gpu_thread_|
  // with the internal implementations below.
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
  struct HdrNetStreamContext {
    enum class Mode {
      kReplaceYuv,
      kAppendWithBlob,
    };

    Mode mode = Mode::kReplaceYuv;

    // The original stream requested by the client.
    camera3_stream_t* original_stream = nullptr;

    // The stream that will be set in place of |original_stream| in capture
    // requests.
    std::unique_ptr<camera3_stream_t> hdrnet_stream;

    // The buffers bound as SharedImage for the |hdrnet_stream|, which will
    // be used in capture requests and for HDRnet processing.
    std::vector<SharedImage> shared_images;

    // Intermediate buffer used in HDRnet processing for still capture requests.
    ScopedBufferHandle still_capture_intermediate;

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

  struct HdrNetRequestBufferInfo {
    HdrNetRequestBufferInfo(HdrNetStreamContext* context,
                            std::vector<camera3_stream_buffer_t>&& buffers);
    HdrNetRequestBufferInfo(HdrNetRequestBufferInfo&& other);
    HdrNetRequestBufferInfo& operator=(HdrNetRequestBufferInfo&& other);
    ~HdrNetRequestBufferInfo();
    void Invalidate();

    static constexpr int kInvalidBufferIndex = -1;

    // The HdrNetStreamContext this request buffer is associated with.
    HdrNetStreamContext* stream_context = nullptr;

    // The index to the |stream_context->shared_images| specifying the
    // SharedImage used in the request
    int buffer_index = -1;

    // The release fence that needs to be waited before accessing the HDRnet
    // buffer returned by the camera HAL.
    base::ScopedFD release_fence;

    // The buffers requested by the client. These buffers will be filled by
    // HDRnet pipeline with the buffers rendered by the pipeline, with
    // downscaling if needed.
    std::vector<camera3_stream_buffer_t> client_requested_yuv_buffers;

    // Indicator for whether the request is pending on a BLOB buffer from the
    // camera HAL. The metadata from the BLOB buffer will be extracted and
    // filled in the final still capture result.
    bool blob_result_pending = false;

    // Indicator for whether the request if pending on a intermediate YUV output
    // from the HDRnet pipeline. The YUV buffer rendered by the HDRnet pipeline
    // is used to produce the final still capture result.
    bool blob_intermediate_yuv_pending = false;

    // Skips the HDRnet processing and directly copies the ISP output to the
    // result buffer. When the tonemap mode is set to CONTRAST_CURVE,
    // GAMMA_VALUE or PRESET_CURVE, we need to disable HDRnet per the API
    // requirement.
    bool skip_hdrnet_processing = false;
  };

  using HdrNetBufferInfoList = std::vector<HdrNetRequestBufferInfo>;
  static HdrNetBufferInfoList::iterator FindMatchingBufferInfo(
      HdrNetBufferInfoList* list, const HdrNetStreamContext* const context);
  HdrNetRequestBufferInfo* GetBufferInfoWithPendingBlobStream(
      int frame_number, const camera3_stream_t* blob_stream);

  // Internal implementations of StreamManipulator.  All these methods are
  // sequenced on the |gpu_thread_|.
  bool InitializeOnGpuThread(const camera_metadata_t* static_info,
                             CaptureResultCallback result_callback);
  bool ConfigureStreamsOnGpuThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnGpuThread(
      Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnGpuThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnGpuThread(Camera3CaptureDescriptor* result);
  bool NotifyOnGpuThread(camera3_notify_msg_t* msg);
  bool FlushOnGpuThread();

  // Check if |raw_result_buffers| has any HdrNet buffer that we need to
  // process. |hdrnet_buffer_to_process| stored the HDRnet buffers that are
  // pending further processing, and |output_buffers_to_client| stores the
  // buffers that will be returned to the camera client as-is.
  void ExtractHdrNetBuffersToProcess(
      int frame_number,
      base::span<const camera3_stream_buffer_t> raw_result_buffers,
      std::vector<camera3_stream_buffer_t>* hdrnet_buffer_to_process,
      std::vector<camera3_stream_buffer_t>* output_buffers_to_client);

  // Prepare the set of client-requested buffers that will be rendered by the
  // HDRnet pipeline.
  bool GetBuffersToRender(HdrNetStreamContext* stream_context,
                          HdrNetRequestBufferInfo* request_buffer_info,
                          std::vector<buffer_handle_t>* buffers_to_write);

  // Callback for the buffers rendered by the HDRnet pipeline.
  void OnBuffersRendered(
      int frame_number,
      HdrNetStreamContext* stream_context,
      HdrNetRequestBufferInfo* request_buffer_info,
      std::vector<camera3_stream_buffer_t>* output_buffers_to_client);

  HdrNetConfig::Options PrepareProcessorConfig(
      Camera3CaptureDescriptor* result,
      const HdrNetRequestBufferInfo& buf_info) const;

  bool SetUpPipelineOnGpuThread();

  void ResetStateOnGpuThread();

  void UpdateRequestSettingsOnGpuThread(Camera3CaptureDescriptor* request);

  void RecordYuvBufferForAeControllerOnGpuThread(int frame_number,
                                                 const SharedImage& yuv_input);

  HdrNetStreamContext* CreateHdrNetStreamContext(camera3_stream_t* requested,
                                                 uint32_t replace_format);
  HdrNetStreamContext* GetHdrNetContextFromRequestedStream(
      camera3_stream_t* requested);
  HdrNetStreamContext* GetHdrNetContextFromHdrNetStream(
      camera3_stream_t* hdrnet);

  void OnOptionsUpdated(const base::Value& json_values);
  void UploadMetrics();

  CameraThread gpu_thread_;
  HdrNetProcessor::Factory hdrnet_processor_factory_;
  ReloadableConfigFile config_;
  HdrNetConfig::Options options_;
  android::CameraMetadata static_info_;

  std::unique_ptr<EglContext> egl_context_;

  std::unique_ptr<StillCaptureProcessor> still_capture_processor_;
  CaptureResultCallback result_callback_;

  // The mapping between original and replacement buffers for in-flight
  // requests.
  std::vector<std::unique_ptr<HdrNetStreamContext>> hdrnet_stream_context_;
  std::map<uint32_t, HdrNetBufferInfoList> request_buffer_info_;
  std::map<camera3_stream_t*, HdrNetStreamContext*> request_stream_mapping_;
  std::map<camera3_stream_t*, HdrNetStreamContext*> result_stream_mapping_;

  HdrnetMetrics hdrnet_metrics_;
  std::unique_ptr<CameraMetrics> camera_metrics_;

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_
