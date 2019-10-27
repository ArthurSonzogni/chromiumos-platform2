/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/v4l2_camera_device.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <limits>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/pattern.h>
#include <base/strings/stringprintf.h>
#include <base/timer/elapsed_timer.h>
#include <camera/camera_metadata.h>

#include "cros-camera/common.h"
#include "hal/usb/camera_characteristics.h"
#include "hal/usb/quirks.h"

namespace cros {

V4L2CameraDevice::V4L2CameraDevice()
    : stream_on_(false), device_info_(DeviceInfo()) {}

V4L2CameraDevice::V4L2CameraDevice(const DeviceInfo& device_info)
    : stream_on_(false), device_info_(device_info) {}

V4L2CameraDevice::~V4L2CameraDevice() {
  device_fd_.reset();
}

int V4L2CameraDevice::Connect(const std::string& device_path) {
  VLOGF(1) << "Connecting device path: " << device_path;
  base::AutoLock l(lock_);
  if (device_fd_.is_valid()) {
    LOGF(ERROR) << "A camera device is opened (" << device_fd_.get()
                << "). Please close it first";
    return -EIO;
  }

  // Since device node may be changed after suspend/resume, we allow to use
  // symbolic link to access device.
  device_fd_.reset(RetryDeviceOpen(device_path, O_RDWR));
  if (!device_fd_.is_valid()) {
    PLOGF(ERROR) << "Failed to open " << device_path;
    return -errno;
  }

  if (!IsCameraDevice(device_path)) {
    LOGF(ERROR) << device_path << " is not a V4L2 video capture device";
    device_fd_.reset();
    return -EINVAL;
  }

  // Get and set format here is used to prevent multiple camera using.
  // UVC driver will acquire lock in VIDIOC_S_FMT and VIDIOC_S_SMT will fail if
  // the camera is being used by a user. The second user will fail in Connect()
  // instead of StreamOn(). Usually apps show better error message if camera
  // open fails. If start preview fails, some apps do not handle it well.
  int ret;
  v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_G_FMT, &fmt));
  if (ret < 0) {
    PLOGF(ERROR) << "Unable to G_FMT";
    return -errno;
  }
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_S_FMT, &fmt));
  if (ret < 0) {
    LOGF(WARNING) << "Unable to S_FMT: " << base::safe_strerror(errno)
                  << ", maybe camera is being used by another app.";
    return -errno;
  }

  // Only set power line frequency when the value is correct.
  if (device_info_.power_line_frequency != PowerLineFrequency::FREQ_ERROR) {
    ret = SetPowerLineFrequency(device_info_.power_line_frequency);
    if (ret < 0) {
      if (IsExternalCamera()) {
        VLOGF(2) << "Ignore SetPowerLineFrequency error for external camera";
      } else {
        return -EINVAL;
      }
    }
  }

  // Initial autofocus state.
  struct v4l2_control control;
  control.id = V4L2_CID_FOCUS_AUTO;
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_G_CTRL, &control));
  if (ret < 0) {
    LOGF(WARNING) << "Failed to get V4L2_CID_FOCUS_AUTO";
    autofocus_supported_ = false;
    autofocus_on_ = false;
  } else {
    autofocus_supported_ = true;
    autofocus_on_ = control.value;
  }
  return 0;
}

void V4L2CameraDevice::Disconnect() {
  base::AutoLock l(lock_);
  stream_on_ = false;
  device_fd_.reset();
  buffers_at_client_.clear();
}

int V4L2CameraDevice::StreamOn(uint32_t width,
                               uint32_t height,
                               uint32_t pixel_format,
                               float frame_rate,
                               bool constant_frame_rate,
                               std::vector<base::ScopedFD>* fds,
                               std::vector<uint32_t>* buffer_sizes) {
  base::AutoLock l(lock_);
  if (!device_fd_.is_valid()) {
    LOGF(ERROR) << "Device is not opened";
    return -ENODEV;
  }
  if (stream_on_) {
    LOGF(ERROR) << "Device has stream already started";
    return -EIO;
  }

  int ret;
  struct v4l2_control control;
  control.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
  control.value = constant_frame_rate ? 0 : 1;
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_S_CTRL, &control));
  if (ret < 0) {
    LOGF(WARNING) << "Failed to set V4L2_CID_EXPOSURE_AUTO_PRIORITY";
  }

  // Some drivers use rational time per frame instead of float frame rate, this
  // constant k is used to convert between both: A fps -> [k/k*A] seconds/frame.
  const int kFrameRatePrecision = 10000;
  v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = pixel_format;
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_S_FMT, &fmt));
  if (ret < 0) {
    PLOGF(ERROR) << "Unable to S_FMT";
    return -errno;
  }
  VLOGF(1) << "Actual width: " << fmt.fmt.pix.width
           << ", height: " << fmt.fmt.pix.height
           << ", pixelformat: " << std::hex << fmt.fmt.pix.pixelformat;

  if (width != fmt.fmt.pix.width || height != fmt.fmt.pix.height ||
      pixel_format != fmt.fmt.pix.pixelformat) {
    LOGF(ERROR) << "Unsupported format: width " << width << ", height "
                << height << ", pixelformat " << pixel_format;
    return -EINVAL;
  }

  // Set capture framerate in the form of capture interval.
  v4l2_streamparm streamparm = {};
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  // The following line checks that the driver knows about framerate get/set.
  if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_G_PARM, &streamparm)) >=
      0) {
    // Now check if the device is able to accept a capture framerate set.
    if (streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
      // |frame_rate| is float, approximate by a fraction.
      streamparm.parm.capture.timeperframe.numerator = kFrameRatePrecision;
      streamparm.parm.capture.timeperframe.denominator =
          (frame_rate * kFrameRatePrecision);

      if (TEMP_FAILURE_RETRY(
              ioctl(device_fd_.get(), VIDIOC_S_PARM, &streamparm)) < 0) {
        LOGF(ERROR) << "Failed to set camera framerate";
        return -EIO;
      }

      VLOGF(1) << "Actual camera driver framerate: "
               << streamparm.parm.capture.timeperframe.denominator << "/"
               << streamparm.parm.capture.timeperframe.numerator;
    }
  }
  float fps =
      static_cast<float>(streamparm.parm.capture.timeperframe.denominator) /
      streamparm.parm.capture.timeperframe.numerator;
  if (std::fabs(fps - frame_rate) > std::numeric_limits<float>::epsilon()) {
    LOGF(ERROR) << "Unsupported frame rate " << frame_rate;
    return -EINVAL;
  }

  v4l2_requestbuffers req_buffers;
  memset(&req_buffers, 0, sizeof(req_buffers));
  req_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req_buffers.memory = V4L2_MEMORY_MMAP;
  req_buffers.count = kNumVideoBuffers;
  if (TEMP_FAILURE_RETRY(
          ioctl(device_fd_.get(), VIDIOC_REQBUFS, &req_buffers)) < 0) {
    PLOGF(ERROR) << "REQBUFS fails";
    return -errno;
  }
  VLOGF(1) << "Requested buffer number: " << req_buffers.count;

  buffers_at_client_.resize(req_buffers.count);
  std::vector<base::ScopedFD> temp_fds;
  for (uint32_t i = 0; i < req_buffers.count; i++) {
    v4l2_exportbuffer expbuf;
    memset(&expbuf, 0, sizeof(expbuf));
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = i;
    if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_EXPBUF, &expbuf)) <
        0) {
      PLOGF(ERROR) << "EXPBUF (" << i << ") fails";
      return -errno;
    }
    VLOGF(1) << "Exported frame buffer fd: " << expbuf.fd;
    temp_fds.push_back(base::ScopedFD(expbuf.fd));
    buffers_at_client_[i] = false;

    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.index = i;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_QBUF, &buffer)) < 0) {
      PLOGF(ERROR) << "QBUF (" << i << ") fails";
      return -errno;
    }

    buffer_sizes->push_back(buffer.length);
  }

  v4l2_buf_type capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (TEMP_FAILURE_RETRY(
          ioctl(device_fd_.get(), VIDIOC_STREAMON, &capture_type)) < 0) {
    PLOGF(ERROR) << "STREAMON fails";
    return -errno;
  }

  for (size_t i = 0; i < temp_fds.size(); i++) {
    fds->push_back(std::move(temp_fds[i]));
  }

  stream_on_ = true;
  return 0;
}

int V4L2CameraDevice::StreamOff() {
  base::AutoLock l(lock_);
  if (!device_fd_.is_valid()) {
    LOGF(ERROR) << "Device is not opened";
    return -ENODEV;
  }
  // Because UVC driver cannot allow STREAMOFF after REQBUF(0), adding a check
  // here to prevent it.
  if (!stream_on_) {
    return 0;
  }

  v4l2_buf_type capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (TEMP_FAILURE_RETRY(
          ioctl(device_fd_.get(), VIDIOC_STREAMOFF, &capture_type)) < 0) {
    PLOGF(ERROR) << "STREAMOFF fails";
    return -errno;
  }
  v4l2_requestbuffers req_buffers;
  memset(&req_buffers, 0, sizeof(req_buffers));
  req_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req_buffers.memory = V4L2_MEMORY_MMAP;
  req_buffers.count = 0;
  if (TEMP_FAILURE_RETRY(
          ioctl(device_fd_.get(), VIDIOC_REQBUFS, &req_buffers)) < 0) {
    PLOGF(ERROR) << "REQBUFS fails";
    return -errno;
  }
  buffers_at_client_.clear();
  stream_on_ = false;
  return 0;
}

int V4L2CameraDevice::GetNextFrameBuffer(uint32_t* buffer_id,
                                         uint32_t* data_size,
                                         uint64_t* v4l2_ts,
                                         uint64_t* user_ts) {
  base::AutoLock l(lock_);
  if (!device_fd_.is_valid()) {
    LOGF(ERROR) << "Device is not opened";
    return -ENODEV;
  }
  if (!stream_on_) {
    LOGF(ERROR) << "Streaming is not started";
    return -EIO;
  }

  if (device_info_.quirks & kQuirkRestartOnTimeout) {
    pollfd device_pfd = {};
    device_pfd.fd = device_fd_.get();
    device_pfd.events = POLLIN;

    constexpr int kCaptureTimeoutMs = 1000;
    const int result =
        TEMP_FAILURE_RETRY(poll(&device_pfd, 1, kCaptureTimeoutMs));

    if (result < 0) {
      PLOGF(ERROR) << "Polling fails";
      return -errno;
    } else if (result == 0) {
      LOGF(ERROR) << "Timed out waiting for captured frame";
      return -ETIMEDOUT;
    }

    if (!(device_pfd.revents & POLLIN)) {
      LOGF(ERROR) << "Unexpected event occurred while polling";
      return -EIO;
    }
  }

  v4l2_buffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_DQBUF, &buffer)) < 0) {
    PLOGF(ERROR) << "DQBUF fails";
    return -errno;
  }
  VLOGF(1) << "DQBUF returns index " << buffer.index << " length "
           << buffer.length;

  if (buffer.index >= buffers_at_client_.size() ||
      buffers_at_client_[buffer.index]) {
    LOGF(ERROR) << "Invalid buffer id " << buffer.index;
    return -EINVAL;
  }

  *buffer_id = buffer.index;
  *data_size = buffer.bytesused;

  struct timeval tv = buffer.timestamp;
  *v4l2_ts = tv.tv_sec * 1'000'000'000LL + tv.tv_usec * 1000;

  struct timespec ts;
  if (clock_gettime(GetUvcClock(), &ts) < 0) {
    LOGF(ERROR) << "Get clock time fails";
    return -errno;
  }

  *user_ts = ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;

  buffers_at_client_[buffer.index] = true;

  return 0;
}

int V4L2CameraDevice::ReuseFrameBuffer(uint32_t buffer_id) {
  base::AutoLock l(lock_);
  if (!device_fd_.is_valid()) {
    LOGF(ERROR) << "Device is not opened";
    return -ENODEV;
  }
  if (!stream_on_) {
    LOGF(ERROR) << "Streaming is not started";
    return -EIO;
  }

  VLOGF(1) << "Reuse buffer id: " << buffer_id;
  if (buffer_id >= buffers_at_client_.size() ||
      !buffers_at_client_[buffer_id]) {
    LOGF(ERROR) << "Invalid buffer id: " << buffer_id;
    return -EINVAL;
  }
  v4l2_buffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = buffer_id;
  if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_QBUF, &buffer)) < 0) {
    PLOGF(ERROR) << "QBUF fails";
    return -errno;
  }
  buffers_at_client_[buffer.index] = false;
  return 0;
}

bool V4L2CameraDevice::IsBufferFilled(uint32_t buffer_id) {
  v4l2_buffer buffer = {};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = buffer_id;
  if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_QUERYBUF, &buffer)) <
      0) {
    PLOGF(ERROR) << "QUERYBUF fails";
    return false;
  }
  return buffer.flags & V4L2_BUF_FLAG_DONE;
}

int V4L2CameraDevice::SetAutoFocus(bool enable) {
  if (!autofocus_supported_ || enable == autofocus_on_)
    return 0;
  int ret;
  struct v4l2_control control;
  control.id = V4L2_CID_FOCUS_AUTO;
  control.value = enable ? 1 : 0;
  ret = TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_S_CTRL, &control));
  if (ret < 0) {
    LOGF(WARNING) << "Failed to set V4L2_CID_FOCUS_AUTO";
  } else {
    autofocus_on_ = enable;
  }
  return ret;
}

// static
const SupportedFormats V4L2CameraDevice::GetDeviceSupportedFormats(
    const std::string& device_path) {
  VLOGF(1) << "Query supported formats for " << device_path;

  base::ScopedFD fd(RetryDeviceOpen(device_path, O_RDONLY));
  if (!fd.is_valid()) {
    PLOGF(ERROR) << "Failed to open " << device_path;
    return {};
  }

  SupportedFormats formats;
  v4l2_fmtdesc v4l2_format = {};
  v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  for (;
       TEMP_FAILURE_RETRY(ioctl(fd.get(), VIDIOC_ENUM_FMT, &v4l2_format)) == 0;
       ++v4l2_format.index) {
    SupportedFormat supported_format;
    supported_format.fourcc = v4l2_format.pixelformat;

    v4l2_frmsizeenum frame_size = {};
    frame_size.pixel_format = v4l2_format.pixelformat;
    for (; HANDLE_EINTR(ioctl(fd.get(), VIDIOC_ENUM_FRAMESIZES, &frame_size)) ==
           0;
         ++frame_size.index) {
      if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        supported_format.width = frame_size.discrete.width;
        supported_format.height = frame_size.discrete.height;
      } else if (frame_size.type == V4L2_FRMSIZE_TYPE_STEPWISE ||
                 frame_size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        // TODO(henryhsu): see http://crbug.com/249953, support these devices.
        LOGF(ERROR) << "Stepwise and continuous frame size are unsupported";
        return formats;
      }

      supported_format.frame_rates = GetFrameRateList(
          fd.get(), v4l2_format.pixelformat, frame_size.discrete.width,
          frame_size.discrete.height);
      formats.push_back(supported_format);
    }
  }
  return formats;
}

// static
std::vector<float> V4L2CameraDevice::GetFrameRateList(int fd,
                                                      uint32_t fourcc,
                                                      uint32_t width,
                                                      uint32_t height) {
  std::vector<float> frame_rates;

  v4l2_frmivalenum frame_interval = {};
  frame_interval.pixel_format = fourcc;
  frame_interval.width = width;
  frame_interval.height = height;
  for (; TEMP_FAILURE_RETRY(
             ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frame_interval)) == 0;
       ++frame_interval.index) {
    if (frame_interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
      if (frame_interval.discrete.numerator != 0) {
        frame_rates.push_back(
            frame_interval.discrete.denominator /
            static_cast<float>(frame_interval.discrete.numerator));
      }
    } else if (frame_interval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS ||
               frame_interval.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
      // TODO(henryhsu): see http://crbug.com/249953, support these devices.
      LOGF(ERROR) << "Stepwise and continuous frame interval are unsupported";
      return frame_rates;
    }
  }
  // Some devices, e.g. Kinect, do not enumerate any frame rates, see
  // http://crbug.com/412284. Set their frame_rate to zero.
  if (frame_rates.empty()) {
    frame_rates.push_back(0);
  }
  return frame_rates;
}

// static
bool V4L2CameraDevice::IsCameraDevice(const std::string& device_path) {
  // RetryDeviceOpen() assumes the device is a camera and waits until the camera
  // is ready, so we use open() instead of RetryDeviceOpen() here.
  base::ScopedFD fd(TEMP_FAILURE_RETRY(open(device_path.c_str(), O_RDONLY)));
  if (!fd.is_valid()) {
    PLOGF(ERROR) << "Failed to open " << device_path;
    return false;
  }

  v4l2_capability v4l2_cap;
  if (TEMP_FAILURE_RETRY(ioctl(fd.get(), VIDIOC_QUERYCAP, &v4l2_cap)) != 0) {
    return false;
  }

  auto check_mask = [](uint32_t caps) {
    const uint32_t kCaptureMask =
        V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
    // Old drivers use (CAPTURE | OUTPUT) for memory-to-memory video devices.
    const uint32_t kOutputMask =
        V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
    const uint32_t kM2mMask = V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE;
    return (caps & kCaptureMask) && !(caps & kOutputMask) && !(caps & kM2mMask);
  };

  // Prefer to use available capabilities of that specific device node instead
  // of the physical device as a whole, so we can properly ignore the metadata
  // device node.
  if (v4l2_cap.capabilities & V4L2_CAP_DEVICE_CAPS) {
    return check_mask(v4l2_cap.device_caps);
  } else {
    return check_mask(v4l2_cap.capabilities);
  }
}

// static
std::string V4L2CameraDevice::GetModelName(const std::string& device_path) {
  auto get_by_interface = [&](std::string* name) {
    base::FilePath real_path;
    if (!base::NormalizeFilePath(base::FilePath(device_path), &real_path)) {
      return false;
    }
    if (!base::MatchPattern(real_path.value(), "/dev/video*")) {
      return false;
    }
    // /sys/class/video4linux/video{N}/device is a symlink to the corresponding
    // USB device info directory.
    auto interface_path = base::FilePath("/sys/class/video4linux")
                              .Append(real_path.BaseName())
                              .Append("device/interface");
    return base::ReadFileToString(interface_path, name);
  };

  auto get_by_cap = [&](std::string* name) {
    base::ScopedFD fd(RetryDeviceOpen(device_path, O_RDONLY));
    if (!fd.is_valid()) {
      PLOGF(WARNING) << "Failed to open " << device_path;
      return false;
    }

    v4l2_capability cap;
    if (TEMP_FAILURE_RETRY(ioctl(fd.get(), VIDIOC_QUERYCAP, &cap)) != 0) {
      PLOGF(WARNING) << "Failed to query capability of " << device_path;
      return false;
    }
    *name = std::string(reinterpret_cast<const char*>(cap.card));
    return true;
  };

  std::string name;
  if (get_by_interface(&name)) {
    return name;
  }
  if (get_by_cap(&name)) {
    return name;
  }
  return "USB Camera";
}

// static
int V4L2CameraDevice::RetryDeviceOpen(const std::string& device_path,
                                      int flags) {
  const int64_t kDeviceOpenTimeOutInMilliseconds = 2000;
  const int64_t kSleepTimeInMilliseconds = 100;
  int fd;
  base::ElapsedTimer timer;
  int64_t elapsed_time = timer.Elapsed().InMillisecondsRoundedUp();
  while (elapsed_time < kDeviceOpenTimeOutInMilliseconds) {
    fd = TEMP_FAILURE_RETRY(open(device_path.c_str(), flags));
    if (fd != -1) {
      // Make sure ioctl is ok. Once ioctl failed, we have to re-open the
      // device.
      struct v4l2_fmtdesc v4l2_format = {};
      v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      int ret = TEMP_FAILURE_RETRY(ioctl(fd, VIDIOC_ENUM_FMT, &v4l2_format));
      if (ret == -1) {
        close(fd);
        if (errno != EPERM) {
          break;
        } else {
          VLOGF(1) << "Camera ioctl is not ready";
        }
      } else {
        // Only return fd when ioctl is ready.
        if (elapsed_time >= kSleepTimeInMilliseconds) {
          LOGF(INFO) << "Opened the camera device after waiting for "
                     << elapsed_time << " ms";
        }
        return fd;
      }
    } else if (errno != ENOENT) {
      break;
    }
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(kSleepTimeInMilliseconds));
    elapsed_time = timer.Elapsed().InMillisecondsRoundedUp();
  }
  PLOGF(ERROR) << "Failed to open " << device_path;
  return -1;
}

// static
clockid_t V4L2CameraDevice::GetUvcClock() {
  static const clockid_t kUvcClock = [] {
    const base::FilePath kClockPath("/sys/module/uvcvideo/parameters/clock");
    std::string clock;
    if (base::ReadFileToString(kClockPath, &clock)) {
      if (clock.find("REALTIME") != std::string::npos) {
        return CLOCK_REALTIME;
      } else if (clock.find("BOOTTIME") != std::string::npos) {
        return CLOCK_BOOTTIME;
      } else {
        return CLOCK_MONOTONIC;
      }
    }
    // Use UVC default clock.
    return CLOCK_MONOTONIC;
  }();
  return kUvcClock;
}

// static
PowerLineFrequency V4L2CameraDevice::GetPowerLineFrequency(
    const std::string& device_path) {
  base::ScopedFD fd(RetryDeviceOpen(device_path, O_RDONLY));
  if (!fd.is_valid()) {
    PLOGF(ERROR) << "Failed to open " << device_path;
    return PowerLineFrequency::FREQ_ERROR;
  }

  struct v4l2_queryctrl query = {};
  query.id = V4L2_CID_POWER_LINE_FREQUENCY;
  if (TEMP_FAILURE_RETRY(ioctl(fd.get(), VIDIOC_QUERYCTRL, &query)) < 0) {
    LOGF(ERROR) << "Power line frequency should support auto or 50/60Hz";
    return PowerLineFrequency::FREQ_ERROR;
  }

  PowerLineFrequency frequency = GetPowerLineFrequencyForLocation();
  if (frequency == PowerLineFrequency::FREQ_DEFAULT) {
    switch (query.default_value) {
      case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
        frequency = PowerLineFrequency::FREQ_50HZ;
        break;
      case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
        frequency = PowerLineFrequency::FREQ_60HZ;
        break;
      case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
        frequency = PowerLineFrequency::FREQ_AUTO;
        break;
      default:
        break;
    }
  }

  // Prefer auto setting if camera module supports auto mode.
  if (query.maximum == V4L2_CID_POWER_LINE_FREQUENCY_AUTO) {
    frequency = PowerLineFrequency::FREQ_AUTO;
  } else if (query.minimum >= V4L2_CID_POWER_LINE_FREQUENCY_60HZ) {
    // TODO(shik): Handle this more gracefully for external camera
    LOGF(ERROR) << "Camera module should at least support 50/60Hz";
    return PowerLineFrequency::FREQ_ERROR;
  }
  return frequency;
}

// static
bool V4L2CameraDevice::IsAutoFocusSupported(const std::string& device_path) {
  base::ScopedFD fd(RetryDeviceOpen(device_path, O_RDONLY));
  if (!fd.is_valid()) {
    PLOGF(ERROR) << "Failed to open " << device_path;
    return false;
  }
  struct v4l2_queryctrl query_ctrl;
  query_ctrl.id = V4L2_CID_FOCUS_AUTO;
  if (TEMP_FAILURE_RETRY(ioctl(fd.get(), VIDIOC_QUERYCTRL, &query_ctrl)) < 0) {
    LOGF(WARNING) << "Failed to qery V4L2_CID_FOCUS_AUTO";
    return false;
  }
  return !(query_ctrl.flags & V4L2_CTRL_FLAG_DISABLED);
}

int V4L2CameraDevice::SetPowerLineFrequency(PowerLineFrequency setting) {
  int v4l2_freq_setting = V4L2_CID_POWER_LINE_FREQUENCY_DISABLED;
  switch (setting) {
    case PowerLineFrequency::FREQ_50HZ:
      v4l2_freq_setting = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
      break;
    case PowerLineFrequency::FREQ_60HZ:
      v4l2_freq_setting = V4L2_CID_POWER_LINE_FREQUENCY_60HZ;
      break;
    case PowerLineFrequency::FREQ_AUTO:
      v4l2_freq_setting = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
      break;
    default:
      LOGF(ERROR) << "Invalid setting for power line frequency: "
                  << static_cast<int>(setting);
      return -EINVAL;
  }

  struct v4l2_control control = {};
  control.id = V4L2_CID_POWER_LINE_FREQUENCY;
  control.value = v4l2_freq_setting;
  if (TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), VIDIOC_S_CTRL, &control)) <
      0) {
    LOGF(ERROR) << "Error setting power line frequency to "
                << v4l2_freq_setting;
    return -EINVAL;
  }
  VLOGF(1) << "Set power line frequency(" << static_cast<int>(setting)
           << ") successfully";
  return 0;
}

bool V4L2CameraDevice::IsExternalCamera() {
  return device_info_.lens_facing == ANDROID_LENS_FACING_EXTERNAL;
}

}  // namespace cros
