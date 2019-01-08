/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <unistd.h>

#include <deque>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/task_runner_util.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/edk/embedder/platform_channel_pair.h>
#include <mojo/edk/embedder/platform_channel_utils_posix.h>
#include <mojo/edk/embedder/platform_handle_vector.h>
#include <mojo/edk/embedder/scoped_platform_handle.h>

#include "hal/usb_v1/arc_camera_service.h"
#include "hal/usb_v1/v4l2_camera_device.h"

namespace arc {

ArcCameraServiceImpl::ArcCameraServiceImpl(int socket_fd, base::Closure quit_cb)
    : socket_fd_(socket_fd),
      quit_cb_(quit_cb),
      binding_(this),
      camera_device_(new V4L2CameraDevice()),
      ipc_thread_("Mojo IPC thread") {
  mojo::edk::Init();
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessageLoop::TYPE_IO, 0))) {
    LOG(ERROR) << "Mojo IPC thread failed to start";
    return;
  }
  mojo::edk::InitIPCSupport(ipc_thread_.task_runner());
}

ArcCameraServiceImpl::~ArcCameraServiceImpl() {
  if (binding_.is_bound())
    binding_.Close();
  camera_device_->Disconnect();
  mojo::edk::ShutdownIPCSupport(base::Bind(&base::DoNothing));
  ipc_thread_.Stop();
}

bool ArcCameraServiceImpl::Start() {
  if (!socket_fd_.is_valid()) {
    LOG(ERROR) << "Invalid socket fd: " << socket_fd_.get();
    return false;
  }
  mojo::edk::ScopedPlatformHandle handle(
      mojo::edk::PlatformHandle(socket_fd_.release()));

  // Make socket blocking.
  int flags = HANDLE_EINTR(fcntl(handle.get().handle, F_GETFL));
  if (flags == -1) {
    PLOG(ERROR) << "fcntl(F_GETFL)";
    return false;
  }
  if (HANDLE_EINTR(fcntl(handle.get().handle, F_SETFL, flags & ~O_NONBLOCK)) ==
      -1) {
    PLOG(ERROR) << "fcntl(F_SETFL) failed to disable O_NONBLOCK";
    return false;
  }

  // The other side will send a one-byte message plus a file descriptor that's
  // going to be used as the parent pipe. This byte is the length of the
  // following message. It will be either zero for the legacy handshake, or 32
  // for the new one, in which case it will be followed by a 32-byte token
  // that's used to create the child MessagePipe.
  constexpr size_t kMojoTokenLength = 32;
  uint8_t buf[kMojoTokenLength] = {};

  std::deque<mojo::edk::PlatformHandle> platform_handles;
  // First, receive a single byte to see what handshake we will receive.
  ssize_t message_length = mojo::edk::PlatformChannelRecvmsg(
      handle.get(), buf, 1, &platform_handles, true);

  if (platform_handles.size() != 1) {
    LOG(ERROR) << "Unexpected number of handles received, expected 1: "
               << platform_handles.size();
    return false;
  }

  mojo::edk::ScopedPlatformHandle parent_pipe(platform_handles.back());
  platform_handles.pop_back();
  if (!parent_pipe.is_valid()) {
    LOG(ERROR) << "Invalid parent pipe";
    return false;
  }
  mojo::edk::SetParentPipeHandle(std::move(parent_pipe));

  mojo::ScopedMessagePipeHandle message_pipe;
  if (buf[0] == kMojoTokenLength) {
    message_length = HANDLE_EINTR(read(handle.get().handle, buf, sizeof(buf)));
    if (message_length == -1) {
      PLOG(ERROR) << "Failed to receive token";
      return false;
    }
    if (message_length != kMojoTokenLength) {
      LOG(ERROR) << "Failed to read full token, only read first "
                 << message_length << " bytes";
      return false;
    }
    message_pipe = mojo::edk::CreateChildMessagePipe(
        std::string(reinterpret_cast<const char*>(buf), message_length));
  } else {
    message_pipe = mojo::edk::ConnectToPeerProcess(std::move(handle));
  }

  // This thread that calls Bind() will receive IPC functions.
  binding_.Bind(std::move(message_pipe));
  binding_.set_connection_error_handler(
      base::Bind(&ArcCameraServiceImpl::OnChannelClosed, base::Unretained(this),
                 "Triggered from binding"));
  return true;
}

void ArcCameraServiceImpl::OnChannelClosed(const std::string& error_msg) {
  VLOG(1) << "Mojo connection lost: " << error_msg;
  if (binding_.is_bound())
    binding_.Close();
  quit_cb_.Run();
}

void ArcCameraServiceImpl::Connect(const std::string& device_path,
                                   const ConnectCallback& callback) {
  VLOG(1) << "Receive Connect message, device_path: " << device_path;
  int ret = camera_device_->Connect(device_path);
  callback.Run(ret);
}

void ArcCameraServiceImpl::Disconnect(const DisconnectCallback& callback) {
  VLOG(1) << "Receive Disconnect message";
  camera_device_->Disconnect();
  callback.Run();
}

void ArcCameraServiceImpl::StreamOn(uint32_t width,
                                    uint32_t height,
                                    uint32_t pixel_format,
                                    float frame_rate,
                                    const StreamOnCallback& callback) {
  VLOG(1) << "Receive StreamOn message, width: " << width
          << ", height: " << height << ", pixel_format: " << pixel_format
          << ", frame_rate: " << frame_rate;
  std::vector<int> fds;
  uint32_t buffer_size;
  int ret = camera_device_->StreamOn(width, height, pixel_format, frame_rate,
                                     &fds, &buffer_size);

  std::vector<mojo::ScopedHandle> handles;
  for (const auto& fd : fds) {
    MojoHandle wrapped_handle;
    MojoResult wrap_result = mojo::edk::CreatePlatformHandleWrapper(
        mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd)),
        &wrapped_handle);
    if (wrap_result != MOJO_RESULT_OK) {
      LOG(ERROR) << "Failed to wrap handles: " << wrap_result;
      ret = -EIO;
    }
    handles.push_back(mojo::ScopedHandle(mojo::Handle(wrapped_handle)));
  }
  if (ret) {
    handles.clear();
  }
  callback.Run(std::move(handles), buffer_size, ret);
}

void ArcCameraServiceImpl::StreamOff(const StreamOffCallback& callback) {
  VLOG(1) << "Receive StreamOff message";
  int ret = camera_device_->StreamOff();
  callback.Run(ret);
}

void ArcCameraServiceImpl::GetNextFrameBuffer(
    const GetNextFrameBufferCallback& callback) {
  VLOG(1) << "Receive GetNextFrameBuffer message";
  uint32_t buffer_id, data_size;
  int ret = camera_device_->GetNextFrameBuffer(&buffer_id, &data_size);
  callback.Run(buffer_id, data_size, ret);
}

void ArcCameraServiceImpl::ReuseFrameBuffer(
    uint32_t buffer_id, const ReuseFrameBufferCallback& callback) {
  VLOG(1) << "Receive ReuseFrameBuffer message, buffer_id: " << buffer_id;
  int ret = camera_device_->ReuseFrameBuffer(buffer_id);
  callback.Run(ret);
}

void ArcCameraServiceImpl::GetDeviceSupportedFormats(
    const std::string& device_path,
    const GetDeviceSupportedFormatsCallback& callback) {
  VLOG(1) << "Receive GetDeviceSupportedFormats message, device_path: "
          << device_path;
  SupportedFormats formats =
      camera_device_->GetDeviceSupportedFormats(device_path);

  std::vector<MojoSupportedFormatPtr> mojo_formats;
  for (const auto& format : formats) {
    MojoSupportedFormatPtr mojo_format = MojoSupportedFormat::New();
    mojo_format->width = format.width;
    mojo_format->height = format.height;
    mojo_format->fourcc = format.fourcc;
    for (const auto& frame_rate : format.frameRates) {
      mojo_format->frameRates.push_back(frame_rate);
    }
    mojo_formats.push_back(std::move(mojo_format));
  }
  callback.Run(std::move(mojo_formats));
}

void ArcCameraServiceImpl::GetCameraDeviceInfos(
    const GetCameraDeviceInfosCallback& callback) {
  VLOG(1) << "Receive GetCameraDeviceInfos message";
  DeviceInfos device_infos = camera_device_->GetCameraDeviceInfos();

  std::vector<MojoDeviceInfoPtr> mojo_device_infos;
  for (const auto& device_info : device_infos) {
    MojoDeviceInfoPtr info = MojoDeviceInfo::New();
    info->device_path = device_info.device_path;
    info->usb_vid = device_info.usb_vid;
    info->usb_pid = device_info.usb_pid;
    info->lens_facing = device_info.lens_facing;
    info->sensor_orientation = device_info.sensor_orientation;
    info->frames_to_skip_after_streamon =
        device_info.frames_to_skip_after_streamon;
    info->horizontal_view_angle_16_9 = device_info.horizontal_view_angle_16_9;
    info->horizontal_view_angle_4_3 = device_info.horizontal_view_angle_4_3;
    // TODO(hidehiko): Remove explicit std::vector<float>(). This is just
    // due to compatibility for before/after uprev.
    info->lens_info_available_focal_lengths =
        std::vector<float>(device_info.lens_info_available_focal_lengths);
    info->lens_info_minimum_focus_distance =
        device_info.lens_info_minimum_focus_distance;
    info->lens_info_optimal_focus_distance =
        device_info.lens_info_optimal_focus_distance;
    info->vertical_view_angle_16_9 = device_info.vertical_view_angle_16_9;
    info->vertical_view_angle_4_3 = device_info.vertical_view_angle_4_3;
    mojo_device_infos.push_back(std::move(info));
  }
  callback.Run(std::move(mojo_device_infos));
}

}  // namespace arc
