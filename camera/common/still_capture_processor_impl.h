/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_STILL_CAPTURE_PROCESSOR_IMPL_H_
#define CAMERA_COMMON_STILL_CAPTURE_PROCESSOR_IMPL_H_

#include "common/still_capture_processor.h"

#include <map>
#include <memory>
#include <vector>

#include <base/callback_helpers.h>
#include <base/threading/thread.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common_types.h"
#include "cros-camera/jpeg_compressor.h"

namespace cros {

class StillCaptureProcessorImpl : public StillCaptureProcessor {
 public:
  explicit StillCaptureProcessorImpl(
      std::unique_ptr<JpegCompressor> jpeg_compressor);

  ~StillCaptureProcessorImpl() override;
  void Initialize(const camera3_stream_t* const still_capture_stream,
                  CaptureResultCallback result_callback) override;
  void Reset() override;
  void QueuePendingOutputBuffer(
      int frame_number,
      camera3_stream_buffer_t output_buffer,
      const camera_metadata_t* request_settings) override;
  void QueuePendingAppsSegments(int frame_number,
                                buffer_handle_t blob_buffer) override;
  void QueuePendingYuvImage(int frame_number,
                            buffer_handle_t yuv_buffer) override;

 private:
  struct RequestContext {
    std::vector<uint8_t> apps_segments_buffer;
    // A look-up table for each of the JPEG markers and their contents in
    // |apps_segments_buffer|.
    std::map<uint16_t, base::span<uint8_t>> apps_segments_index;
    bool has_apps_segments = false;

    std::vector<uint8_t> thumbnail_buffer;
    Size thumbnail_size = {0, 0};
    int thumbnail_quality = 80;

    ScopedBufferHandle jpeg_blob;
    bool has_jpeg = false;
    uint32_t jpeg_blob_size = 0;
    int jpeg_quality = 95;

    camera3_stream_buffer_t client_requested_buffer;
  };

  void QueuePendingOutputBufferOnThread(int frame_number,
                                        RequestContext request_context);
  void QueuePendingAppsSegmentsOnThread(
      int frame_number,
      std::vector<uint8_t> apps_segments_buffer,
      std::map<uint16_t, base::span<uint8_t>> apps_segments_index);
  void QueuePendingYuvImageOnThread(int frame_number,
                                    buffer_handle_t yuv_buffer);
  void MaybeProduceCaptureResultOnThread(int frame_number);

  base::Thread thread_;
  std::unique_ptr<JpegCompressor> jpeg_compressor_;

  const camera3_stream_t* blob_stream_ = nullptr;
  CaptureResultCallback result_callback_ = base::NullCallback();

  // Bookkeeping the RequestContext using the frame number as index.
  std::map<int, RequestContext> request_contexts_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_STILL_CAPTURE_PROCESSOR_IMPL_H_
