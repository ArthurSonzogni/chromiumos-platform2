/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/buffer_dumper_stream_manipulator.h"

#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>

#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_buffer_utils.h"

namespace cros {

BufferDumperStreamManipulator::BufferDumperStreamManipulator(
    base::FilePath dump_folder, base::TimeDelta dump_period)
    : dump_folder_(std::move(dump_folder)),
      dump_period_(dump_period),
      thread_("BufferDumperSM") {
  CHECK(thread_.Start());
}

BufferDumperStreamManipulator::~BufferDumperStreamManipulator() {
  thread_.Stop();
}

bool BufferDumperStreamManipulator::Initialize(
    const camera_metadata_t* static_info, Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool BufferDumperStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  // For each camera session, dump to a subfolder named by timestamp.
  base::Time::Exploded exploded;
  base::Time::Now().UTCExplode(&exploded);
  CHECK(exploded.HasValidValues());
  base::FilePath subfolder(base::StringPrintf(
      "%04d%02d%02d-%02d%02d%02d%03d", exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second,
      exploded.millisecond));
  dump_folder_per_session_ = dump_folder_.Append(base::FilePath(subfolder));
  CHECK(base::CreateDirectory(dump_folder_per_session_));
  return true;
}

bool BufferDumperStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool BufferDumperStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool BufferDumperStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

bool BufferDumperStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  CHECK(thread_.task_runner()->BelongsToCurrentThread());

  auto is_format_supported = [](const camera3_stream_t* stream) {
    return stream->format == HAL_PIXEL_FORMAT_BLOB ||
           ((stream->format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
             stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) &&
            !(stream->usage & GRALLOC_USAGE_HW_CAMERA_READ));
  };

  for (auto& b : result.GetMutableOutputBuffers()) {
    if (!is_format_supported(b.stream()) ||
        (stream_to_timer_.contains(b.stream()) &&
         stream_to_timer_[b.stream()].Elapsed() < dump_period_)) {
      continue;
    }
    stream_to_timer_[b.stream()] = base::ElapsedTimer();

    constexpr int kSyncWaitTimeoutMs = 300;
    CHECK(b.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs));

    const base::FilePath path = MakeDumpPath(result.frame_number(), b.stream());
    LOGF(INFO) << "Dump buffer to " << path << " ("
               << GetDebugString(b.stream()) << ")";
    CHECK(WriteBufferIntoFile(*b.buffer(), path));
  }

  callbacks_.result_callback.Run(std::move(result));
  return true;
}

void BufferDumperStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(msg);
}

bool BufferDumperStreamManipulator::Flush() {
  return true;
}

scoped_refptr<base::SingleThreadTaskRunner>
BufferDumperStreamManipulator::GetTaskRunner() {
  return thread_.task_runner();
}

base::FilePath BufferDumperStreamManipulator::MakeDumpPath(
    uint32_t frame_number, const camera3_stream_t* stream) {
  std::string file_name = base::StringPrintf(
      "%u_%p_%ux%u.%s", frame_number, stream, stream->width, stream->height,
      stream->format == HAL_PIXEL_FORMAT_BLOB ? "jpg" : "yuv");
  return base::FilePath(dump_folder_per_session_)
      .Append(base::FilePath(std::move(file_name)));
}

}  // namespace cros
