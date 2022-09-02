/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sync/sync.h>
#include <utility>

#include "cros-camera/common.h"
#include "hal/fake/metadata_handler.h"
#include "hal/fake/request_handler.h"

namespace cros {

namespace {

uint64_t CurrentTimestamp() {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    PLOGF(ERROR) << "Get clock time fails";
    // TODO(pihsun): Handle error
    return 0;
  }

  return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

}  // namespace

RequestHandler::RequestHandler(
    const int id,
    const camera3_callback_ops_t* callback_ops,
    const android::CameraMetadata& static_metadata,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner)
    : id_(id),
      callback_ops_(callback_ops),
      task_runner_(task_runner),
      static_metadata_(static_metadata) {}

RequestHandler::~RequestHandler() = default;

void RequestHandler::HandleRequest(std::unique_ptr<CaptureRequest> request) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  uint32_t frame_number = request->GetFrameNumber();
  VLOGFID(1, id_) << "Request Frame: " << frame_number;

  auto& buffers = request->GetStreamBuffers();

  // TODO(pihsun): Determine the appropriate timeout for the sync wait.
  const int kSyncWaitTimeoutMs = 300;

  for (auto& buffer : buffers) {
    if (buffer.acquire_fence == -1) {
      continue;
    }

    int ret = sync_wait(buffer.acquire_fence, kSyncWaitTimeoutMs);
    if (ret != 0) {
      // If buffer is not ready, set |release_fence| to notify framework to
      // wait the buffer again.
      buffer.release_fence = buffer.acquire_fence;
      LOGFID(ERROR, id_) << "Fence sync_wait failed: " << buffer.acquire_fence;
      HandleAbortedRequest(*request);
      return;
    } else {
      close(buffer.acquire_fence);
    }

    // HAL has to set |acquire_fence| to -1 for output buffers.
    buffer.acquire_fence = -1;
  }

  for (auto& buffer : buffers) {
    if (!FillResultBuffer(buffer)) {
      LOGFID(ERROR, id_) << "failed to fill buffer, aborting request";
      HandleAbortedRequest(*request);
      return;
    }
  }

  NotifyShutter(frame_number);

  android::CameraMetadata result_metadata = request->GetMetadata();
  CHECK(FillResultMetadata(&result_metadata).ok());

  camera3_capture_result_t capture_result = {
      .frame_number = frame_number,
      .result = result_metadata.getAndLock(),
      .num_output_buffers = base::checked_cast<uint32_t>(buffers.size()),
      .output_buffers = buffers.data(),
      .partial_result = 1,
  };

  // After process_capture_result, HAL cannot access the output buffer in
  // camera3_stream_buffer anymore unless the release fence is not -1.
  callback_ops_->process_capture_result(callback_ops_, &capture_result);
}

bool RequestHandler::FillResultBuffer(camera3_stream_buffer_t& buffer) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto it = fake_streams_.find(buffer.stream);
  if (it == fake_streams_.end()) {
    LOGF(ERROR) << "Unknown stream " << buffer.stream;
    return false;
  }
  return it->second->FillBuffer(*buffer.buffer);
}

void RequestHandler::StreamOn(const std::vector<camera3_stream_t*>& streams,
                              base::OnceCallback<void(absl::Status)> callback) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto ret = StreamOnImpl(streams);
  std::move(callback).Run(ret);
}

void RequestHandler::StreamOff(
    base::OnceCallback<void(absl::Status)> callback) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto ret = StreamOffImpl();
  std::move(callback).Run(ret);
}

absl::Status RequestHandler::StreamOnImpl(
    const std::vector<camera3_stream_t*>& streams) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  fake_streams_.clear();

  for (auto stream : streams) {
    Size size(stream->width, stream->height);

    auto fake_stream =
        FakeStream::Create(static_metadata_, size,
                           static_cast<android_pixel_format_t>(stream->format));
    if (fake_stream == nullptr) {
      return absl::InternalError("error initializing fake stream");
    }

    fake_streams_.emplace(stream, std::move(fake_stream));
  }

  return absl::OkStatus();
}

absl::Status RequestHandler::StreamOffImpl() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  fake_streams_.clear();

  return absl::OkStatus();
}

void RequestHandler::HandleAbortedRequest(CaptureRequest& request) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto& buffers = request.GetStreamBuffers();
  for (auto buffer : buffers) {
    buffer.status = CAMERA3_BUFFER_STATUS_ERROR;
  }

  uint32_t frame_number = request.GetFrameNumber();

  camera3_capture_result_t capture_result = {
      .frame_number = frame_number,
      .num_output_buffers = static_cast<uint32_t>(buffers.size()),
      .output_buffers = buffers.data(),
  };

  NotifyRequestError(frame_number);
  callback_ops_->process_capture_result(callback_ops_, &capture_result);
}

void RequestHandler::NotifyShutter(uint32_t frame_number) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  camera3_notify_msg_t msg = {
      .type = CAMERA3_MSG_SHUTTER,
      .message =
          {
              .shutter =
                  {
                      .frame_number = frame_number,
                      .timestamp = CurrentTimestamp(),
                  },
          },
  };

  callback_ops_->notify(callback_ops_, &msg);
}

void RequestHandler::NotifyRequestError(uint32_t frame_number) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  camera3_notify_msg_t msg = {
      .type = CAMERA3_MSG_ERROR,
      .message =
          {
              .error =
                  {
                      .frame_number = frame_number,
                      .error_code = CAMERA3_MSG_ERROR_REQUEST,
                  },
          },
  };

  callback_ops_->notify(callback_ops_, &msg);
}
}  // namespace cros
