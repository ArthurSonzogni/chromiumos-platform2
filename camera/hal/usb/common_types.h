/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_COMMON_TYPES_H_
#define CAMERA_HAL_USB_COMMON_TYPES_H_

#include <string>
#include <vector>

#include "cros-camera/timezone.h"

namespace cros {

// Fields without default value would be filled in runtime.

struct DeviceInfo {
  int camera_id;

  // TODO(shik): Change this to base::FilePath.
  // ex: /dev/video0
  std::string device_path;

  // Whether the device is an emulated vivid camera.
  bool is_vivid;

  // USB vendor id, the emulated vivid devices do not have this field.
  std::string usb_vid;

  // USB product id, the emulated vivid devices do not have this field.
  std::string usb_pid;

  // Some cameras need to wait several frames to output correct images.
  uint32_t frames_to_skip_after_streamon = 0;

  // Power line frequency supported by device, which will be filled according to
  // the current location instead of camera_characteristics.conf.
  PowerLineFrequency power_line_frequency = PowerLineFrequency::FREQ_DEFAULT;

  // The camera doesn't support constant frame rate. That means HAL cannot set
  // V4L2_CID_EXPOSURE_AUTO_PRIORITY to 0 to have constant frame rate in low
  // light environment.
  bool constant_framerate_unsupported = false;

  // Member definitions can be found in https://developer.android.com/
  // reference/android/hardware/camera2/CameraCharacteristics.html
  uint32_t lens_facing;
  int32_t sensor_orientation = 0;

  // Special settings for device specific quirks.
  uint32_t quirks = 0;

  // These fields are not available for external cameras.
  std::vector<float> lens_info_available_apertures;
  std::vector<float> lens_info_available_focal_lengths;
  float lens_info_minimum_focus_distance;
  float lens_info_optimal_focus_distance;
  int32_t sensor_info_pixel_array_size_width;
  int32_t sensor_info_pixel_array_size_height;
  float sensor_info_physical_size_width;
  float sensor_info_physical_size_height;
};

typedef std::vector<DeviceInfo> DeviceInfos;

struct SupportedFormat {
  uint32_t width;
  uint32_t height;
  uint32_t fourcc;
  // All the supported frame rates in fps with given width, height, and
  // pixelformat. This is not sorted. For example, suppose width, height, and
  // fourcc are 640x480 YUYV. If frame rates are 15.0 and 30.0, the camera
  // supports outputting 640x480 YUYV in 15fps or 30fps.
  std::vector<float> frame_rates;

  uint32_t area() const { return width * height; }
  bool operator<(const SupportedFormat& rhs) const {
    if (area() == rhs.area()) {
      return width < rhs.width;
    }
    return area() < rhs.area();
  }
  bool operator==(const SupportedFormat& rhs) const {
    return width == rhs.width && height == rhs.height;
  }
};

typedef std::vector<SupportedFormat> SupportedFormats;

struct Size {
  Size(uint32_t w, uint32_t h) : width(w), height(h) {}
  uint32_t width;
  uint32_t height;
  uint32_t area() const { return width * height; }
  bool operator<(const Size& rhs) const {
    if (area() == rhs.area()) {
      return width < rhs.width;
    }
    return area() < rhs.area();
  }
  bool operator==(const Size& rhs) const {
    return width == rhs.width && height == rhs.height;
  }
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_COMMON_TYPES_H_
