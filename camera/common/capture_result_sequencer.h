/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAPTURE_RESULT_SEQUENCER_H_
#define CAMERA_COMMON_CAPTURE_RESULT_SEQUENCER_H_

#include <hardware/camera3.h>

#include <optional>

#include <base/containers/flat_map.h>
#include <base/sequence_checker.h>

#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"

namespace cros {

//
// CaptureResultSequencer is an adapter over StreamManipulator::Callbacks that
// manages the calling sequence and may split/merge capture results to ensure
// buffers on each stream are returned in order.
//
// The class methods need to be called in sequence, except for the constructor
// and SetCallbacks.
//
class CaptureResultSequencer {
 public:
  explicit CaptureResultSequencer(StreamManipulator::Callbacks callbacks);
  CaptureResultSequencer(const CaptureResultSequencer&) = delete;
  CaptureResultSequencer& operator=(const CaptureResultSequencer&) = delete;
  ~CaptureResultSequencer();

  // Inspect an in-coming capture request before stream manipulator processing.
  void AddRequest(const Camera3CaptureDescriptor& request);

  // Return a stream manipulator processed capture result.
  void AddResult(Camera3CaptureDescriptor result);

  void Notify(camera3_notify_msg_t msg);

  // Drop all the pending requests and buffers.
  void Reset();

 private:
  void SendPendingBuffers() VALID_CONTEXT_REQUIRED(sequence_checker_);

  StreamManipulator::Callbacks callbacks_;
  base::flat_map<const camera3_stream_t*,
                 base::flat_map<uint32_t /*frame_number*/,
                                std::optional<Camera3StreamBuffer>>>
      pending_buffers_ GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAPTURE_RESULT_SEQUENCER_H_
