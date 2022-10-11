// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/camera_client.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <linux/videodev2.h>

#include "faced/camera/camera_service.h"
#include "faced/camera/frame.h"
#include "faced/camera/frame_utils.h"
#include "faced/util/status.h"

namespace faced {

std::string FourccToString(uint32_t fourcc) {
  uint32_t fourcc_processing = fourcc;
  std::string result;
  for (size_t i = 0; i < 4; i++, fourcc_processing >>= 8) {
    const char c = static_cast<char>(fourcc_processing & 0xFF);

    // If any character in the code is non-printable, don't attempt to decode
    // any of it, but just return the entire code as a hex string.
    if (!std::isprint(c)) {
      return base::StringPrintf("0x%08x", fourcc);
    }
    result.push_back(c);
  }
  return result;
}

bool IsFormatEqual(const cros_cam_format_info_t& fmt1,
                   const cros_cam_format_info_t& fmt2) {
  return fmt1.fourcc == fmt2.fourcc && fmt1.width == fmt2.width &&
         fmt1.height == fmt2.height && fmt1.fps == fmt2.fps;
}

absl::StatusOr<std::unique_ptr<CameraClient>> CameraClient::Create(
    std::unique_ptr<CameraService> camera_service) {
  // Establishes a connection with the cros camera service
  if (camera_service->Init() != 0) {
    return absl::UnavailableError("Failed to initialise camera client");
  }

  // Probes the cros camera service for information around what cameras and
  // formats are available for capture
  std::unique_ptr<CameraClient> camera_client =
      base::WrapUnique(new CameraClient(std::move(camera_service)));
  FACE_RETURN_IF_ERROR(camera_client->ProbeAndPrintCameraInfo());

  return camera_client;
}

absl::Status CameraClient::ProbeAndPrintCameraInfo() {
  // GetCameraInfo sends out all existing camera info via the callback
  // synchronously before returning a result, so there are no multithreaded
  // implications. Additionally, GetCameraInfo registers a callback so that
  // future updates can be called asynchronously
  if (camera_service_->GetCameraInfo(&CameraClient::GetCamInfoCallback, this) !=
      0) {
    return absl::NotFoundError("Failed to get camera info");
  }

  // camera_info_frozen_ is set to ignore future asynchronous calls to the
  // callback
  camera_info_frozen_ = true;
  return absl::OkStatus();
}

int CameraClient::GetCamInfoCallback(void* context,
                                     const cros_cam_info_t* info,
                                     int is_removed) {
  auto* client = reinterpret_cast<CameraClient*>(context);
  return client->GotCameraInfo(info, is_removed);
}

int CameraClient::GotCameraInfo(const cros_cam_info_t* info, int is_removed) {
  // Ignore all asynchronous calls from hotplugging
  if (camera_info_frozen_) {
    return 0;
  }

  if (is_removed) {
    camera_infos_.erase(info->id);
    LOG(INFO) << "Camera removed: " << info->id;
    return 0;
  }

  LOG(INFO) << "Gotten camera info of " << info->id << " (name = " << info->name
            << ", format_count = " << info->format_count << ")";
  for (int i = 0; i < info->format_count; i++) {
    LOG(INFO) << "format = " << FourccToString(info->format_info[i].fourcc)
              << ", width = " << info->format_info[i].width
              << ", height = " << info->format_info[i].height
              << ", fps = " << info->format_info[i].fps;
    if (camera_infos_.count(info->id) == 0) {
      camera_infos_[info->id] = std::vector<cros_cam_info_t>();
      LOG(INFO) << "Camera added: " << info->id;
    }

    camera_infos_[info->id].push_back(*info);
  }

  return 0;
}

void CameraClient::CaptureFrames(
    const CaptureFramesConfig& config,
    const scoped_refptr<FrameProcessor>& frame_processor,
    StopCaptureCallback capture_complete) {
  camera_id_ = config.camera_id;
  // Perform a copy since cros_cam_capture_request_t::format needs to be
  // non-const.
  format_ = config.format;

  // Create a cancelable callback which can be cancelled to stop any future
  // frames from being processed
  process_frame_callback_.Reset(
      base::BindRepeating(&FrameProcessor::ProcessFrame, frame_processor));

  capture_complete_ = std::move(capture_complete);

  if (!FormatIsAvailable(config.camera_id, config.format)) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(capture_complete_),
            absl::NotFoundError(absl::StrFormat(
                "Unable to find capture for device = %d, fourcc = %s, "
                "width = %d, height = %d, fps = %d",
                config.camera_id, FourccToString(config.format.fourcc),
                config.format.width, config.format.height,
                config.format.fps))));
    return;
  }

  LOG(INFO) << "Starting capture: device = " << config.camera_id
            << ", fourcc = " << FourccToString(config.format.fourcc)
            << ", width = " << config.format.width
            << ", height = " << config.format.height
            << ", fps = " << config.format.fps;

  // Start the capture.
  const cros_cam_capture_request_t request = {
      .id = camera_id_,
      .format = &format_,
  };

  int ret = camera_service_->StartCapture(
      &request, &CameraClient::OnCaptureResultAvailable, this);
  if (ret != 0) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(capture_complete_),
                       absl::InternalError("Failed to start capture")));
    return;
  }
}

int CameraClient::OnCaptureResultAvailable(
    void* context, const cros_cam_capture_result_t* result) {
  auto* client = reinterpret_cast<CameraClient*>(context);

  if (result->status != 0) {
    LOG(ERROR) << "Received an error notification: "
               << base::safe_strerror(-result->status);
    return 0;
  }
  const cros_cam_frame_t* frame = result->frame;
  CHECK_NE(frame, nullptr);

  base::RepeatingCallback<void(std::unique_ptr<Frame>,
                               ProcessFrameDoneCallback)>
      callback = client->process_frame_callback_.callback();

  // If callback has been cancelled, then return -1 to inform the CameraHAL to
  // stop capturing.
  if (callback.is_null()) {
    return -1;
  }

  // Continue if callback exists.
  if (client->pending_request_) {
    LOG(WARNING) << "Frame dropped since there is already an in-flight frame "
                    "process request.";
    return 0;
  }

  client->pending_request_ = true;
  client->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(callback, FrameFromCrosFrame(*frame),
                     base::BindOnce(&CameraClient::CompletedProcessFrame,
                                    base::Unretained(client))));
  return 0;
}

void CameraClient::CompletedProcessFrame(
    std::optional<absl::Status> opt_status) {
  if (opt_status.has_value()) {
    LOG(INFO) << "Stopping capture on camera: " << camera_id_;
    // Cancel the callback which will result in OnCaptureResultAvailable() to
    // return -1, informing the CameraHAL to stop capturing any more frames.
    // Note that we require one additional frame from the CameraHAL in order
    // to stop the CameraHAL capture and complete the CaptureFrames() call
    process_frame_callback_.Cancel();
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(capture_complete_), opt_status.value()));
  }

  pending_request_ = false;
}

bool CameraClient::FormatIsAvailable(int32_t camera_id,
                                     cros_cam_format_info_t info) {
  if (camera_infos_.count(camera_id) == 0) {
    return false;
  }

  for (const cros_cam_info_t& stored_cam_info : camera_infos_[camera_id]) {
    for (int i = 0; i < stored_cam_info.format_count; i++) {
      if (IsFormatEqual(info, stored_cam_info.format_info[i])) {
        return true;
      }
    }
  }

  return false;
}

std::optional<cros_cam_format_info_t>
CameraClient::GetMaxSupportedResolutionFormat(
    int32_t camera_id,
    uint32_t fourcc,
    std::function<bool(int width, int height)> is_supported) {
  std::optional<cros_cam_format_info_t> res = std::nullopt;

  for (const cros_cam_info_t& stored_cam_info : camera_infos_[camera_id]) {
    for (int i = 0; i < stored_cam_info.format_count; i++) {
      const cros_cam_format_info_t& stored_cam_format =
          stored_cam_info.format_info[i];

      if (!is_supported(stored_cam_format.width, stored_cam_format.height)) {
        continue;
      }

      if (fourcc == stored_cam_format.fourcc) {
        if (!res.has_value() ||
            (res->width * res->height <
             stored_cam_format.width * stored_cam_format.height)) {
          res = stored_cam_format;
        }
      }
    }
  }

  return res;
}

}  // namespace faced
