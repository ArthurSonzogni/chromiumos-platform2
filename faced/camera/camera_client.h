// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_CAMERA_CAMERA_CLIENT_H_
#define FACED_CAMERA_CAMERA_CLIENT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/cancelable_callback.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <base/task/thread_pool.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/threading/thread.h>
#include <unordered_map>

#include "faced/camera/face_cli_camera_service_interface.h"
#include "faced/proto/face_service.pb.h"

namespace faceauth {

const char kTokenPath[] = "/run/camera_tokens/testing/token";
using StopCaptureCallback = base::OnceCallback<void(absl::Status)>;
using ProcessFrameDoneCallback =
    base::OnceCallback<void(std::optional<absl::Status>)>;

// Converts a fourcc (four character code) to a Drm format name string
//
// For example, FourccToString(0x43724f53) is "CrOS".
std::string FourccToString(uint32_t fourcc);

// Checks if two cros_cam_format_info_t types are the same
bool IsFormatEqual(const cros_cam_format_info_t& fmt1,
                   const cros_cam_format_info_t& fmt2);

// Abstract interface for the class that processes incoming frames.
class CameraFrameProcessor
    : public base::RefCountedThreadSafe<CameraFrameProcessor> {
 public:
  virtual ~CameraFrameProcessor() = default;

  // Overriden function to process frames.
  //
  // This function will be called by the CameraClient once for each frame until
  // `done` is called with a status.
  //
  // If `done` is called with `std::nullopt`, the CameraClient will continue
  // capture.
  //
  // It is guaranteed that the CameraClient will call this function in a single
  // sequence and never twice at once.
  //
  // If a second frame arrives from the camera prior to this function returning,
  // `done`, then that frame will be dropped.
  virtual void ProcessFrame(std::unique_ptr<eora::CameraFrame> frame,
                            ProcessFrameDoneCallback done) = 0;

 private:
  friend class base::RefCountedThreadSafe<CameraFrameProcessor>;
};

// CameraClient communicates with cros-camera-service to extract camera frames
class CameraClient {
 public:
  // Config struct for setting parameters for capture
  struct CaptureFramesConfig {
    int32_t camera_id;  // Camera id for capture
    cros_cam_format_info_t
        format;  // Requested format for capture. Format
                 // encapsulates resolution, file type and fps.
  };

  // Factory method: creates and returns a CameraClient using a path to a
  // permission token to access the cros camera service
  //
  // Once created, the CameraClient will have probed the cros camera service for
  // information about available cameras and be ready to capture frames.
  //
  // On success, the returned pointer is guaranteed to be non-null.
  static absl::StatusOr<std::unique_ptr<CameraClient>> Create(
      base::StringPiece token_path_string);

  // Construct CameraClient using the given camera service.
  //
  // CameraClient has ownership of `camera_service`
  static absl::StatusOr<std::unique_ptr<CameraClient>> Create(
      std::unique_ptr<FaceCliCameraServiceInterface> camera_service);

  // CameraClient is not copyable
  CameraClient(const CameraClient&) = delete;
  CameraClient& operator=(const CameraClient&) = delete;

  // Start capturing and processing frames from the camera.
  //
  // This function calls frame_processor->ProcessFrame each time a new frame
  // arrives.
  //
  // The frame_processor `ProcessFrame` implementation should return quickly,
  // performing any long-running actions asynchronously
  void CaptureFrames(const CaptureFramesConfig& config,
                     const scoped_refptr<CameraFrameProcessor>& frame_processor,
                     StopCaptureCallback capture_complete);

  // Checks if a particular camera id and format info is available
  bool FormatIsAvailable(int32_t camera_id, cros_cam_format_info_t info);

  // Given a specified camera_id and fourcc returns the largest resolution
  // format available that is supported as defined by the input is_supported
  // function.
  //
  // Largest resolution is defined by the largest width * height.
  // If there are no formats, return std::nullopt
  std::optional<cros_cam_format_info_t> GetMaxSupportedResolutionFormat(
      int32_t camera_id,
      uint32_t fourcc,
      std::function<bool(int width, int height)> is_supported);

 private:
  // CameraClient can only be constructed via CameraClient::Create()
  explicit CameraClient(
      std::unique_ptr<FaceCliCameraServiceInterface> camera_service)
      : task_runner_(base::SequencedTaskRunnerHandle::Get()),
        camera_service_connector_(std::move(camera_service)) {}

  // Gets and prints the details of each camera
  absl::Status ProbeAndPrintCameraInfo();

  // Callback on receiving camera info
  int GotCameraInfo(const cros_cam_info_t* info, int is_removed);

  // Callback for the cros-camera-service to process camera info when it arrives
  static int GetCamInfoCallback(void* context,
                                const cros_cam_info_t* info,
                                int is_removed);

  // Callback for the cros-camera-service to process camera captures when they
  // arrive
  //
  // Returns 0 if more frames should be captured
  static int OnCaptureResultAvailable(void* context,
                                      const cros_cam_capture_result_t* result);

  // Callback to mark completion of a single process frame operation.
  //
  // Calling with std::nullopt continues captures
  // Calling with any status, stops capture and calls capture_complete_ with
  // that status
  void CompletedProcessFrame(std::optional<absl::Status> opt_status);

  // Camera info is frozen after calling ProbeCameraInfo()
  bool camera_info_frozen_ = false;
  std::unordered_map<int32_t, std::vector<cros_cam_info_t>> camera_infos_;

  // Details about an active capture
  int camera_id_ = 0;
  cros_cam_format_info_t format_;
  bool pending_request_ =
      false;  // If there is a pending process frame request, any frames
              // received from the camera will be dropped

  // Callback called when a frame is received
  base::CancelableRepeatingCallback<void(std::unique_ptr<eora::CameraFrame>,
                                         ProcessFrameDoneCallback)>
      process_frame_callback_;
  scoped_refptr<base::SequencedTaskRunner>
      task_runner_;  // Task runner to call the process frame callback. This
                     // needs to be specified because the process frame is
                     // called from the CameraHAL's thread
  StopCaptureCallback capture_complete_;

  std::unique_ptr<FaceCliCameraServiceInterface> camera_service_connector_ =
      nullptr;
};

}  // namespace faceauth

#endif  // FACED_CAMERA_CAMERA_CLIENT_H_
