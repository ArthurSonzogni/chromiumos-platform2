/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/capture_result_sequencer.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/sequence_checker.h>

#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "cros-camera/common.h"

namespace cros {

CaptureResultSequencer::CaptureResultSequencer(
    StreamManipulator::Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  CHECK(!callbacks_.result_callback.is_null());
  CHECK(!callbacks_.notify_callback.is_null());
}

CaptureResultSequencer::~CaptureResultSequencer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  Reset();
}

void CaptureResultSequencer::AddRequest(
    const Camera3CaptureDescriptor& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& b : request.GetOutputBuffers()) {
    pending_buffers_[b.stream()][request.frame_number()] = std::nullopt;
  }
}

void CaptureResultSequencer::AddResult(Camera3CaptureDescriptor result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& b : result.AcquireOutputBuffers()) {
    if (b.status() == CAMERA3_BUFFER_STATUS_OK) {
      CHECK_NE(pending_buffers_[b.stream()].count(result.frame_number()), 0);
      pending_buffers_[b.stream()][result.frame_number()] = std::move(b);
    } else {
      pending_buffers_[b.stream()].erase(result.frame_number());
      result.AppendOutputBuffer(std::move(b));
    }
  }

  SendPendingBuffers(result.is_empty() ? nullptr : &result);
}

void CaptureResultSequencer::Notify(camera3_notify_msg_t msg) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (msg.type == CAMERA3_MSG_ERROR) {
    const camera3_error_msg_t& err = msg.message.error;
    switch (err.error_code) {
      case CAMERA3_MSG_ERROR_DEVICE:
        pending_buffers_.clear();
        break;
      case CAMERA3_MSG_ERROR_REQUEST:
        for (auto& [s, pending_buffers_on_stream] : pending_buffers_) {
          pending_buffers_on_stream.erase(err.frame_number);
        }
        break;
      case CAMERA3_MSG_ERROR_BUFFER:
        pending_buffers_[err.error_stream].erase(err.frame_number);
        break;
      default:
        break;
    }
  }
  callbacks_.notify_callback.Run(std::move(msg));

  // Buffers blocked by the error frame can be sent.
  SendPendingBuffers();
}

void CaptureResultSequencer::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int num_unsent_buffers = 0, num_unreceived_buffers = 0;
  for (auto& [s, pending_buffers_on_stream] : pending_buffers_) {
    for (auto& [f, b] : pending_buffers_on_stream) {
      if (b.has_value()) {
        ++num_unsent_buffers;
      } else {
        ++num_unreceived_buffers;
      }
    }
  }
  if (num_unsent_buffers != 0 || num_unreceived_buffers != 0) {
    LOGF(WARNING) << "CaptureResultSequencer resetted when there are still "
                  << num_unsent_buffers << " unsent buffers and "
                  << num_unreceived_buffers << " unreceived buffers";
  }
  pending_buffers_.clear();
}

void CaptureResultSequencer::SendPendingBuffers(
    Camera3CaptureDescriptor* pending_result) {
  base::flat_map<uint32_t /*frame_number*/, std::vector<Camera3StreamBuffer>>
      buffers_to_send;
  for (auto& [s, pending_buffers_on_stream] : pending_buffers_) {
    while (!pending_buffers_on_stream.empty() &&
           pending_buffers_on_stream.begin()->second.has_value()) {
      buffers_to_send[pending_buffers_on_stream.begin()->first].push_back(
          std::move(pending_buffers_on_stream.begin()->second.value()));
      pending_buffers_on_stream.erase(pending_buffers_on_stream.begin());
    }
  }
  for (auto& [f, bs] : buffers_to_send) {
    Camera3CaptureDescriptor result(
        camera3_capture_result_t{.frame_number = f});
    if (pending_result != nullptr && pending_result->frame_number() == f) {
      result = std::move(*pending_result);
      pending_result = nullptr;
    }
    for (auto& b : bs) {
      result.AppendOutputBuffer(std::move(b));
    }
    callbacks_.result_callback.Run(std::move(result));
  }
  if (pending_result != nullptr) {
    callbacks_.result_callback.Run(std::move(*pending_result));
  }
}

}  // namespace cros
