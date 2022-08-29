/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sync/sync.h>

#include <utility>

#include "common/test_support/fake_still_capture_processor.h"

namespace cros::tests {

void FakeStillCaptureProcessor::Initialize(
    const camera3_stream_t* const still_capture_stream,
    CaptureResultCallback result_callback) {
  result_callback_ = std::move(result_callback);
}

void FakeStillCaptureProcessor::Reset() {}

void FakeStillCaptureProcessor::QueuePendingOutputBuffer(
    int frame_number,
    camera3_stream_buffer_t output_buffer,
    const camera_metadata_t* request_settings) {
  EXPECT_EQ(result_descriptor_.count(frame_number), 0);
  result_descriptor_.insert({frame_number, ResultDescriptor()});
}

void FakeStillCaptureProcessor::QueuePendingAppsSegments(
    int frame_number,
    buffer_handle_t blob_buffer,
    base::ScopedFD release_fence) {
  ASSERT_EQ(result_descriptor_.count(frame_number), 1);
  if (release_fence.is_valid()) {
    ASSERT_EQ(sync_wait(release_fence.get(), 300), 0);
  }
  result_descriptor_[frame_number].has_apps_segments = true;
  MaybeProduceCaptureResult(frame_number);
}

void FakeStillCaptureProcessor::QueuePendingYuvImage(
    int frame_number,
    buffer_handle_t yuv_buffer,
    base::ScopedFD release_fence) {
  ASSERT_EQ(result_descriptor_.count(frame_number), 1);
  if (release_fence.is_valid()) {
    ASSERT_EQ(sync_wait(release_fence.get(), 1000), 0);
  }
  result_descriptor_[frame_number].has_yuv_buffer = true;
  MaybeProduceCaptureResult(frame_number);
}

void FakeStillCaptureProcessor::MaybeProduceCaptureResult(int frame_number) {
  if (result_descriptor_[frame_number].has_apps_segments &&
      result_descriptor_[frame_number].has_yuv_buffer) {
    result_callback_.Run(Camera3CaptureDescriptor(camera3_capture_result_t{
        .frame_number = static_cast<uint32_t>(frame_number)}));
  }
}

}  // namespace cros::tests
