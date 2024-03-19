/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/capture_result_sequencer.h"

#include <utility>
#include <vector>

#include <base/sequence_checker.h>

#include "common/stream_manipulator.h"
#include "cros-camera/common.h"

namespace cros {

CaptureResultSequencer::CaptureResultSequencer(
    StreamManipulator::Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

CaptureResultSequencer::~CaptureResultSequencer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!pending_buffers_.empty()) {
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
    LOGF(WARNING) << "CaptureResultSequencer destructed when there's still "
                  << num_unsent_buffers << " unsent buffers and "
                  << num_unreceived_buffers << " unreceived buffers";
  }
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
    CHECK_NE(pending_buffers_[b.stream()].count(result.frame_number()), 0);
    pending_buffers_[b.stream()][result.frame_number()] = std::move(b);
  }
  if (!result.is_empty()) {
    callbacks_.result_callback.Run(std::move(result));
  }

  SendPendingBuffers();
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

  callbacks_ = {};
  pending_buffers_.clear();
}

void CaptureResultSequencer::SendPendingBuffers() {
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
    result.SetOutputBuffers(std::move(bs));
    callbacks_.result_callback.Run(std::move(result));
  }
}

}  // namespace cros
