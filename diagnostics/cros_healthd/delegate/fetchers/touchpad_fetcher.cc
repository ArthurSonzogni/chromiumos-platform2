// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "diagnostics/cros_healthd/delegate/fetchers/touchpad_fetcher.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/types/expected.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_enumerate.h>
#include <brillo/udev/udev_list_entry.h>

#include "diagnostics/cros_healthd/delegate/fetchers/constants.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {
namespace mojom = ::ash::cros_healthd::mojom;

std::vector<std::string> SplitFilePath(const std::string& filepath) {
  return base::SplitString(filepath, "/", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

std::string CharToNonNullStr(const char* sequence) {
  return sequence ? sequence : std::string();
}

mojom::InputDevicePtr CreateInternalTouchpadInputDevice(
    const brillo::UdevDevice& dev, const std::string& location) {
  auto input_device = mojom::InputDevice::New();
  input_device->name =
      CharToNonNullStr(dev.GetPropertyValue(touchpad::kUdevPropertyDevname));
  input_device->connection_type = mojom::InputDevice::ConnectionType::kInternal;
  input_device->is_enabled = true;
  input_device->physical_location = location;

  return input_device;
}

base::expected<std::string, std::string> GetDriverSymlinkTarget(
    const brillo::UdevDevice& dev, const std::string& root_path) {
  std::string major =
      CharToNonNullStr(dev.GetPropertyValue(touchpad::kUdevPropertyMajor));
  std::string minor =
      CharToNonNullStr(dev.GetPropertyValue(touchpad::kUdevPropertyMinor));
  if (major.empty() || minor.empty())
    return base::unexpected(
        "Unable to get major/minor numbers from device properties");

  std::string driver_path = base::StrCat(
      {root_path, "sys/dev/char/", major, ":", minor, "/device/device/driver"});

  base::FilePath target;

  if (!base::ReadSymbolicLink(base::FilePath{driver_path}, &target))
    return base::unexpected(
        base::StrCat({"Error reading symbolic link at ", driver_path}));

  if (target.empty())
    return base::unexpected("Error reading driver symlink target");

  return base::ok(target.value());
}

std::string GetPsmouseDriver(const brillo::UdevDevice& dev,
                             const std::string& root_path) {
  std::string serio_port;
  // For the psmouse touchpad type, DEVPATH typically looks like
  // /devices/platform/i8042/serioN/input/input22/event15.
  // Iterate through the file parts to find what N value serioN has.
  for (auto filepath_part : SplitFilePath(CharToNonNullStr(
           dev.GetPropertyValue(touchpad::kUdevPropertyDevpath)))) {
    if (filepath_part.starts_with("serio")) {
      serio_port = filepath_part;
      break;
    }
  }

  std::string protocol_result;
  std::string protocol_path = base::StrCat(
      {root_path, "sys/bus/serio/devices/", serio_port, "/protocol"});

  base::FilePath protocol_filepath{protocol_path};

  if (!base::ReadFileToString(protocol_filepath, &protocol_result))
    return "psmouse";

  std::vector<std::string_view> protocol = base::SplitStringPiece(
      protocol_result, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (protocol.empty()) {
    LOG(WARNING) << "Could not read protocol from protocol path: "
                 << protocol_path;
    return "psmouse";
  }

  return base::StrCat({protocol[0], " psmouse"});
}

bool IsInternalTouchpad(const brillo::UdevDevice& dev) {
  std::string id_input_touchpad_str = CharToNonNullStr(
      dev.GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad));
  int id_input_touchpad;
  bool is_touchpad;
  bool is_internal =
      CharToNonNullStr(dev.GetPropertyValue(touchpad::kUdevPropertyIdBus)) !=
      "usb";
  if (id_input_touchpad_str.empty()) {
    is_touchpad = false;
  } else {
    if (!base::StringToInt(id_input_touchpad_str, &id_input_touchpad)) {
      LOG(WARNING) << "Invalid value for "
                   << touchpad::kUdevPropertyIdInputTouchpad;
      return false;
    }
    // Compare id_input_touchpad to 1 as this signals the device
    // is registered as a touchpad
    is_touchpad = id_input_touchpad == 1;
  }
  bool is_device_handler =
      CharToNonNullStr(dev.GetSysName()).find("event") != std::string::npos;

  return is_touchpad && is_device_handler && is_internal;
}
}  // namespace

base::expected<std::vector<ash::cros_healthd::mojom::TouchpadDevicePtr>,
               std::string>
PopulateTouchpadDevices(std::unique_ptr<brillo::Udev> udev,
                        std::string root_path) {
  if (udev == nullptr)
    return base::unexpected("Error initializing udev");

  std::unique_ptr<brillo::UdevEnumerate> udev_enumerate =
      udev->CreateEnumerate();
  if (udev_enumerate == nullptr)
    return base::unexpected("Error initializing udev_enumerate");

  if (!udev_enumerate->AddMatchSubsystem(kSubsystemInput) ||
      !udev_enumerate->ScanDevices())
    return base::unexpected("Failed to scan input devices");

  for (std::unique_ptr<brillo::UdevListEntry> entry =
           udev_enumerate->GetListEntry();
       entry; entry = entry->GetNext()) {
    std::string sys_path = CharToNonNullStr(entry->GetName());
    if (sys_path.empty()) {
      LOG(WARNING) << "Found an empty syspath for udev device entry. Skipping.";
      continue;
    }

    std::unique_ptr<brillo::UdevDevice> dev =
        udev->CreateDeviceFromSysPath(sys_path.c_str());
    if (!dev) {
      LOG(WARNING) << "Unable to create device from syspath: "
                   << sys_path.c_str();
      continue;
    }
    if (!IsInternalTouchpad(*dev))
      continue;

    auto symlink_target_result = GetDriverSymlinkTarget(*dev, root_path);
    if (!symlink_target_result.has_value())
      return base::unexpected(symlink_target_result.error());

    std::string symlink_target = symlink_target_result.value();
    std::string driver_name;

    if (symlink_target.find("psmouse") != std::string::npos) {
      driver_name = GetPsmouseDriver(*dev, root_path);
    } else {
      std::vector<std::string> filepath_list = SplitFilePath(symlink_target);
      if (filepath_list.empty())
        return base::unexpected(
            "Touchpad symlink was empty. Could not read driver name");

      driver_name = filepath_list.back();
    }

    auto input_device = CreateInternalTouchpadInputDevice(*dev, sys_path);

    auto touchpad_device = mojom::TouchpadDevice::New();
    touchpad_device->input_device = std::move(input_device);
    touchpad_device->driver_name = driver_name;

    std::vector<mojom::TouchpadDevicePtr> devices = {};
    devices.push_back(std::move(touchpad_device));

    return base::ok<std::vector<mojom::TouchpadDevicePtr>>(std::move(devices));
  }
  return base::ok<std::vector<mojom::TouchpadDevicePtr>>({});
}
}  // namespace diagnostics
