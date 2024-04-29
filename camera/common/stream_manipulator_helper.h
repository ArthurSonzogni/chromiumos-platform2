/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_STREAM_MANIPULATOR_HELPER_H_
#define CAMERA_COMMON_STREAM_MANIPULATOR_HELPER_H_

#include <camera/camera_metadata.h>
#include <cutils/native_handle.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/camera_metadata.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check_op.h>
#include <base/containers/flat_map.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>

#include "common/camera_buffer_pool.h"
#include "common/camera_hal3_helpers.h"
#include "common/capture_result_sequencer.h"
#include "common/still_capture_processor.h"
#include "common/stream_manipulator.h"

namespace cros {

constexpr uint32_t kProcessStreamUsageFlags = GRALLOC_USAGE_HW_CAMERA_WRITE |
                                              GRALLOC_USAGE_HW_TEXTURE |
                                              GRALLOC_USAGE_HW_COMPOSER;

class ProcessTask;

using ScopedProcessTask =
    std::unique_ptr<ProcessTask, base::OnTaskRunnerDeleter>;
using OnProcessTaskCallback = base::RepeatingCallback<void(ScopedProcessTask)>;

using CropScaleImageCallback =
    base::RepeatingCallback<std::optional<base::ScopedFD>(
        buffer_handle_t input,
        base::ScopedFD input_release_fence,
        buffer_handle_t output,
        base::ScopedFD output_acquire_fence,
        const Rect<float>& crop)>;

struct StreamFormat {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  float max_fps = 0.0f;
  RelativeFov fov;
};

// Specifies the streams that the stream manipulator is processing on. Depending
// on the process mode and the streams configured/requested, one process task
// for video and/or one for still capture is provided to the stream manipulator
// via callbacks.
enum class ProcessMode {
  // No-op. No stream manipulation and processing.
  kBypass,
  // Process on video and still captures.
  kVideoAndStillProcess,
  // Process on only still captures.
  kStillProcess,
};

// StreamManipulatorHelper implements common stream manipulation logics.
// Implementation of StreamManipulator can hold an instance of this class and
// delegate StreamManipulator APIs to it.
//
// This class is thread-safe. Every function or callback is posted to the given
// task runner.
class StreamManipulatorHelper {
 public:
  // Configures StreamManipulatorHelper behavior.
  struct Config {
    // The stream configuration mode described in ProcessMode. In bypass mode
    // all the other configs are ignored.
    ProcessMode process_mode = ProcessMode::kBypass;

    // Attempt to configure processing streams of larger resolution than the
    // outputs. Process tasks can get larger input resolution than the output
    // resolution.
    bool prefer_large_source = false;

    // If |prefer_large_source| is true, limits the maximum video source stream
    // dimensions. They are soft bounds; if the maximum width/height of client
    // streams is larger, then bounded to it instead.
    std::optional<uint32_t> max_enlarged_video_source_width;
    std::optional<uint32_t> max_enlarged_video_source_height;

    // For video processing, keep the client YUV streams that are generated from
    // the processing stream in the stream config. This allows video stream
    // buffers to be bypassed as-is at runtime, but the HAL needs to support
    // more stream combinations.
    bool preserve_client_video_streams = true;

    // Result metadata tags that will be copied and carried to process tasks
    // for visibility and modification.
    std::vector<uint32_t> result_metadata_tags_to_update;

    bool enable_debug_logs = false;
  };

  // Base class for per-capture private context that can be carried from
  // HandleRequest() to ProcessTask's.
  class PrivateContext {
   public:
    virtual ~PrivateContext() = default;
  };

  // Created in StreamManipulator::Initialize.
  StreamManipulatorHelper(
      Config config,
      const std::string& camera_module_name,
      const camera_metadata_t* static_info,
      StreamManipulator::Callbacks callbacks,
      OnProcessTaskCallback on_process_task,
      CropScaleImageCallback crop_scale_image,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor,
      scoped_refptr<base::SequencedTaskRunner> task_runner);
  StreamManipulatorHelper(const StreamManipulatorHelper&) = delete;
  StreamManipulatorHelper& operator=(const StreamManipulatorHelper&) = delete;
  ~StreamManipulatorHelper();

  // Called in StreamManipulator::ConfigureStreams. If the stream combination
  // can't be supported, this function returns false, left the stream config
  // unmodified, and this helper will act like bypass mode.
  bool PreConfigure(Camera3StreamConfiguration* stream_config);

  // Called in StreamManipulator::OnConfiguredStreams.
  void PostConfigure(Camera3StreamConfiguration* stream_config);

  // Called in StreamManipulator::ProcessCaptureRequest.
  void HandleRequest(Camera3CaptureDescriptor* request,
                     bool bypass_process,
                     std::unique_ptr<PrivateContext> private_context);

  // Called in StreamManipulator::ProcessCaptureResult.
  void HandleResult(Camera3CaptureDescriptor result);

  // Called in StreamManipulator::Notify.
  void Notify(camera3_notify_msg_t msg);

  // Called in StreamManipulator::Flush.
  void Flush();

  // Getters for static metadata.
  const Size& active_array_size() const { return active_array_size_; }

  // Getters for configured states.
  bool stream_config_unsupported() const { return stream_config_unsupported_; }
  const camera3_stream_t* still_process_input_stream() const {
    return still_process_input_stream_.has_value()
               ? still_process_input_stream_->ptr()
               : nullptr;
  }
  const camera3_stream_t* video_process_input_stream() const {
    return video_process_input_stream_.has_value()
               ? video_process_input_stream_->ptr()
               : nullptr;
  }
  const Size& still_process_output_size() const {
    CHECK(still_process_output_size_.has_value());
    return still_process_output_size_.value();
  }
  const Size& video_process_output_size() const {
    CHECK(video_process_output_size_.has_value());
    return video_process_output_size_.value();
  }

 private:
  class OwnedOrExternalStream {
   public:
    explicit OwnedOrExternalStream(camera3_stream_t owned) : owned_(owned) {}
    explicit OwnedOrExternalStream(camera3_stream_t* external)
        : external_(external) {
      CHECK_NE(external_, nullptr);
    }

    bool owned() const { return owned_.has_value(); }
    camera3_stream_t* ptr() { return owned() ? &owned_.value() : external_; }
    const camera3_stream_t* ptr() const {
      return owned() ? &owned_.value() : external_;
    }

   private:
    std::optional<camera3_stream_t> owned_;
    camera3_stream_t* external_ = nullptr;
  };

  // Configured usage for client streams.
  enum class StreamType {
    kIgnored,
    kBlob,
    kStillYuvToProcess,
    kStillYuvToGenerate,
    kVideoYuvToProcess,
    kVideoYuvToGenerate,
  };

  // State of a stream buffer in one capture.
  enum class StreamState {
    // The buffer is requested to the lower layer and not yet returned.
    kRequesting,
    // The buffer is received from the lower layer, but pending on metadata to
    // start processing.
    kPending,
    // The buffer is under processing.
    kProcessing,
    // The buffer is received, done processing, and released.
    kDone,
  };

  // Per-stream context in one capture.
  struct StreamContext {
    bool for_process = false;
    StreamState state = StreamState::kRequesting;
    std::optional<CameraBufferPool::Buffer> pool_process_input;
    std::optional<CameraBufferPool::Buffer> pool_process_output;
    std::optional<Camera3StreamBuffer> process_input;
    std::optional<Camera3StreamBuffer> process_output;
    std::vector<Camera3StreamBuffer> client_yuv_buffers_to_generate;
  };

  // Per-capture context.
  struct CaptureContext {
    base::flat_map<const camera3_stream_t*, StreamContext> requested_streams;
    bool still_capture_cancelled = false;
    bool last_result_metadata_received = false;
    bool last_result_metadata_sent = false;
    std::optional<Camera3StreamBuffer> client_buffer_for_blob;
    std::optional<CameraBufferPool::Buffer> pool_buffer_for_blob;
    android::CameraMetadata result_metadata;
    FeatureMetadata feature_metadata;
    std::unique_ptr<PrivateContext> private_context;

    bool Done() const;
  };

  struct SourceStreamInfo {
    OwnedOrExternalStream stream;
    float max_scaling_factor = 0.0f;
  };

  const StreamFormat& GetFormat(const camera3_stream_t& stream) const;
  std::optional<SourceStreamInfo> FindSourceStream(
      base::span<camera3_stream_t* const> dst_yuv_streams,
      bool for_still_capture) const;
  // Returns capture context on the frame, and a scoped callback that removes
  // the context if it's Done().
  std::pair<CaptureContext&, base::ScopedClosureRunner /*ctx_remover*/>
  GetCaptureContext(uint32_t frame_number);
  void ReturnCaptureResult(Camera3CaptureDescriptor result,
                           CaptureContext& capture_ctx);
  void CropScaleImages(Camera3StreamBuffer& src_buffer,
                       base::span<Camera3StreamBuffer> dst_buffers);
  void Reset();
  void OnProcessTaskDone(ProcessTask& task);
  void OnStillCaptureResult(Camera3CaptureDescriptor result);

  Config config_;
  std::unique_ptr<CaptureResultSequencer> result_sequencer_;
  OnProcessTaskCallback on_process_task_;
  CropScaleImageCallback crop_scale_image_;
  std::unique_ptr<StillCaptureProcessor> still_capture_processor_;

  // Static metadata.
  uint32_t partial_result_count_ = 0;
  Size active_array_size_;
  std::vector<StreamFormat> available_formats_;

  // Configured states.
  bool stream_config_unsupported_ = false;
  base::flat_map<camera3_stream_t*, StreamType> client_stream_to_type_;
  std::optional<OwnedOrExternalStream> still_process_input_stream_;
  std::optional<OwnedOrExternalStream> video_process_input_stream_;
  std::optional<Size> blob_size_;
  std::optional<Size> still_process_output_size_;
  std::optional<Size> video_process_output_size_;
  std::unique_ptr<CameraBufferPool> blob_sized_buffer_pool_;
  std::unique_ptr<CameraBufferPool> still_process_input_pool_;
  std::unique_ptr<CameraBufferPool> still_process_output_pool_;
  std::unique_ptr<CameraBufferPool> video_process_input_pool_;
  std::unique_ptr<CameraBufferPool> video_process_output_pool_;
  std::optional<camera3_stream_t> fake_still_process_output_stream_;
  std::optional<camera3_stream_t> fake_video_process_output_stream_;
  std::optional<StreamFormat> fake_still_process_output_format_;
  std::optional<StreamFormat> fake_video_process_output_format_;

  // Per-frame states.
  // Use unique_ptr for pointer stability since process tasks reference it.
  base::flat_map<uint32_t /*frame_number*/, std::unique_ptr<CaptureContext>>
      capture_contexts_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

// A ProcessTask is sent to the StreamManipulator via OnProcessTaskCallback when
// there's a video or still capture to be processed. Destructing ProcessTask
// means the processing is done and returns it back to the
// StreamManipulatorHelper.
//
// This class is thread-safe in that all the referenced data is not accessed by
// StreamManipulatorHelper during processing. Process tasks of the same frame
// number reference to the same result metadata, feature metadata and private
// context.
// TODO(kamesan): Have some check in the helper to enforce the thread-safety.
class ProcessTask {
 public:
  ProcessTask(const ProcessTask&) = delete;
  ProcessTask& operator=(ProcessTask&) = delete;
  ~ProcessTask() { std::move(on_process_task_done_).Run(*this); }

  // Whether this task is for still capture. Otherwise it's for video.
  bool IsStillCapture() const {
    return input_->stream()->usage & kStillCaptureUsageFlag;
  }

  // Returns the release fence that should be waited before the input buffer can
  // be read.
  base::ScopedFD TakeInputReleaseFence() {
    return base::ScopedFD(input_->take_release_fence());
  }

  // Returns the acquire fence that should be waited before the output buffer
  // can be written.
  base::ScopedFD TakeOutputAcquireFence() {
    return base::ScopedFD(output_->take_acquire_fence());
  }

  // Sets the release fence for writes to the output buffer to be done.
  void SetOutputReleaseFence(base::ScopedFD fence) {
    output_->mutable_raw_buffer().release_fence = fence.release();
  }

  // Fails this task. The related output stream buffers will be returned to the
  // client with error status.
  void Fail() {
    output_->mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_ERROR;
  }

  // Gets the private context passed to HandleRequest().
  template <typename T>
  T* GetPrivateContextAs() const {
    return static_cast<T*>(private_context_);
  }

  uint32_t frame_number() const { return frame_number_; }
  const camera3_stream_t* input_stream() const { return input_->stream(); }
  buffer_handle_t input_buffer() const { return *input_->buffer(); }
  buffer_handle_t output_buffer() const { return *output_->buffer(); }
  Size input_size() const {
    return Size(input_->stream()->width, input_->stream()->height);
  }
  Size output_size() const {
    return Size(output_->stream()->width, output_->stream()->height);
  }
  android::CameraMetadata& result_metadata() { return *result_metadata_; }
  FeatureMetadata& feature_metadata() { return *feature_metadata_; }

 private:
  friend class StreamManipulatorHelper;

  using OnProcessTaskDoneCallback = base::OnceCallback<void(ProcessTask&)>;

  ProcessTask(uint32_t frame_number,
              Camera3StreamBuffer* input,
              Camera3StreamBuffer* output,
              android::CameraMetadata* result_metadata,
              FeatureMetadata* feature_metadata,
              StreamManipulatorHelper::PrivateContext* private_context,
              OnProcessTaskDoneCallback on_process_task_done)
      : frame_number_(frame_number),
        input_(input),
        output_(output),
        result_metadata_(result_metadata),
        feature_metadata_(feature_metadata),
        private_context_(private_context),
        on_process_task_done_(std::move(on_process_task_done)) {
    CHECK_NE(input_, nullptr);
    CHECK_EQ(input_->status(), CAMERA3_BUFFER_STATUS_OK);
    CHECK_NE(output_, nullptr);
    CHECK_EQ(output_->status(), CAMERA3_BUFFER_STATUS_OK);
    CHECK_NE(result_metadata_, nullptr);
    CHECK_NE(feature_metadata_, nullptr);
    CHECK(!on_process_task_done_.is_null());
  }

  uint32_t frame_number_ = 0;
  Camera3StreamBuffer* input_ = nullptr;
  Camera3StreamBuffer* output_ = nullptr;
  android::CameraMetadata* result_metadata_ = nullptr;
  FeatureMetadata* feature_metadata_ = nullptr;
  StreamManipulatorHelper::PrivateContext* private_context_ = nullptr;

  OnProcessTaskDoneCallback on_process_task_done_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_STREAM_MANIPULATOR_HELPER_H_
