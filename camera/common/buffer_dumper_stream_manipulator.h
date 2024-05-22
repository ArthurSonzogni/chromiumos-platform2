/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_BUFFER_DUMPER_STREAM_MANIPULATOR_H_
#define CAMERA_COMMON_BUFFER_DUMPER_STREAM_MANIPULATOR_H_

#include <base/containers/flat_map.h>
#include <base/files/file_path.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "common/stream_manipulator.h"

namespace cros {

// Dump the stream buffers for debugging. For example, put the following into
// stream_manipulator_manager.cc so it samples a buffer every 1 second and
// writes it to /run/camera/dump/ for every stream between the 2 stream
// manipulators.
//
//   // ... Upper stream manipulator ...
//
//   stream_manipulators_.emplace_back(
//       std::make_unique<BufferDumperStreamManipulator>(
//           base::FilePath("/run/camera/dump"), base::Seconds(1)));
//
//   // ... Lower stream manipulator ...
//
class BufferDumperStreamManipulator : public StreamManipulator {
 public:
  BufferDumperStreamManipulator(base::FilePath dump_folder,
                                base::TimeDelta dump_period);
  BufferDumperStreamManipulator(const BufferDumperStreamManipulator&) = delete;
  BufferDumperStreamManipulator& operator=(
      const BufferDumperStreamManipulator&) = delete;
  ~BufferDumperStreamManipulator() override;

  // StreamManipulator implementation.
  bool Initialize(const camera_metadata_t* static_info,
                  Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() override;

 private:
  base::FilePath MakeDumpPath(uint32_t frame_number,
                              const camera3_stream_t* stream);

  Callbacks callbacks_;
  base::FilePath dump_folder_;
  base::FilePath dump_folder_per_session_;
  base::TimeDelta dump_period_;
  base::flat_map<const camera3_stream_t*, base::ElapsedTimer> stream_to_timer_;
  base::Thread thread_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_BUFFER_DUMPER_STREAM_MANIPULATOR_H_
