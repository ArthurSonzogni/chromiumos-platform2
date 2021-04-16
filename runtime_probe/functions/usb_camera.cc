// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/usb_camera.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

namespace {
constexpr char kDevVideoPath[] = "/dev/video*";
constexpr char kSysVideoPath[] = "/sys/class/video4linux";

using FieldsType = std::vector<std::pair<std::string, std::string>>;

const FieldsType kRequiredFields{{"usb_vendor_id", "idVendor"},
                                 {"usb_product_id", "idProduct"}};
const FieldsType kOptionalFields{{"usb_manufacturer", "manufacturer"},
                                 {"usb_product", "product"},
                                 {"usb_bcd_device", "bcdDevice"},
                                 {"usb_removable", "removable"}};

bool IsCaptureDevice(const base::FilePath& path) {
  int32_t fd = open(path.value().c_str(), O_RDONLY);
  if (fd == -1) {
    LOG(ERROR) << "Failed to open " << path;
    return false;
  }

  v4l2_capability cap;
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap, 1) == -1) {
    LOG(ERROR) << "Failed to execute ioctl to query the V4L2 capability";
    return false;
  }
  if (close(fd) == -1) {
    LOG(ERROR) << "Failed to close " << path;
  }

  uint32_t mask = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                            : cap.capabilities;
  return (mask & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) &&
         !(mask & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE)) &&
         !(mask & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE));
}

bool ReadUsbSysfs(const base::FilePath& path, base::Value* res) {
  const auto device_name = path.BaseName();
  // The path "/sys/class/video4linux/*/device" is a symbolic link. Get the real
  // absolute path before |MapFilesToDict|.
  const auto device_path =
      base::FilePath(kSysVideoPath).Append(device_name).Append("device/..");
  const auto sysfs_dir_path = base::MakeAbsoluteFilePath(device_path);
  if (sysfs_dir_path.empty()) {
    LOG(ERROR) << "Failed to get absolute file path from: " << device_path;
    return false;
  }
  auto result =
      MapFilesToDict(sysfs_dir_path, kRequiredFields, kOptionalFields);
  if (!result) {
    LOG(ERROR) << "Failed to read files from: " << sysfs_dir_path;
    return false;
  }

  auto* removable = result->FindStringKey("usb_removable");
  if (removable) {
    *removable = base::ToUpperASCII(*removable);
  }
  res->MergeDictionary(&*result);
  return true;
}

bool ExploreAsUsbCamera(const base::FilePath& path, base::Value* res) {
  return IsCaptureDevice(path) && ReadUsbSysfs(path, res);
}

}  // namespace

UsbCameraFunction::DataType UsbCameraFunction::EvalImpl() const {
  UsbCameraFunction::DataType result{};

  for (const auto& video_path : Glob(kDevVideoPath)) {
    base::Value res(base::Value::Type::DICTIONARY);
    res.SetStringKey("path", video_path.value());
    if (ExploreAsUsbCamera(video_path, &res)) {
      res.SetStringKey("bus_type", "usb");
      result.push_back(std::move(res));
    }
  }

  return result;
}

}  // namespace runtime_probe
