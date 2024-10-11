// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/display/display_watcher.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include <base/containers/circular_deque.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "power_manager/common/tracing.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager::system {

namespace {

// Path containing directories describing the state of DRM devices.
const char kSysClassDrmPath[] = "/sys/class/drm";

// Glob-style pattern for device directories within kSysClassDrmPath.
const char kDrmDeviceNamePattern[] = "card*";

// Glob-style pattern for the I2C device name within a DRM device directory.
const char kI2CDeviceNamePattern[] = "i2c-*";

// Directory containing I2C devices.
const char kI2CDevPath[] = "/dev";

// The delay to advertise about change in display configuration after udev
// event.
constexpr base::TimeDelta kDebounceDelay = base::Seconds(1);

// Returns true if the device described by |drm_device_dir| is connected.
bool IsConnectorStatus(const base::FilePath& drm_device_dir,
                       const std::string& connector_status) {
  base::FilePath status_path =
      drm_device_dir.Append(DisplayWatcher::kDrmStatusFile);
  std::string status;
  if (!base::ReadFileToString(status_path, &status)) {
    return false;
  }

  // Trim whitespace to deal with trailing newlines.
  base::TrimWhitespaceASCII(status, base::TRIM_TRAILING, &status);
  return status == connector_status;
}

}  // namespace

const char DisplayWatcher::kI2CUdevSubsystem[] = "i2c-dev";
const char DisplayWatcher::kDrmUdevSubsystem[] = "drm";
const char DisplayWatcher::kDrmStatusFile[] = "status";
const char DisplayWatcher::kDrmStatusConnected[] = "connected";
const char DisplayWatcher::kDrmStatusUnknown[] = "unknown";

DisplayWatcher::DisplayWatcher()
    : sysfs_drm_path_(base::FilePath(kSysClassDrmPath)),
      i2c_dev_path_(base::FilePath(kI2CDevPath)) {}

DisplayWatcher::~DisplayWatcher() {
  if (udev_) {
    udev_->RemoveSubsystemObserver(kI2CUdevSubsystem, this);
    udev_->RemoveSubsystemObserver(kDrmUdevSubsystem, this);
    udev_ = nullptr;
  }
}

bool DisplayWatcher::trigger_debounce_timeout_for_testing() {
  if (!debounce_timer_.IsRunning()) {
    return false;
  }
  debounce_timer_.Stop();
  HandleDebounceTimeout();
  return true;
}

void DisplayWatcher::Init(UdevInterface* udev) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kI2CUdevSubsystem, this);
  udev_->AddSubsystemObserver(kDrmUdevSubsystem, this);
  UpdateDisplays(NO_DEBOUNCE);
}

const std::vector<DisplayInfo>& DisplayWatcher::GetDisplays() const {
  return displays_;
}

void DisplayWatcher::AddObserver(DisplayWatcherObserver* observer) {
  observers_.AddObserver(observer);
}

void DisplayWatcher::RemoveObserver(DisplayWatcherObserver* observer) {
  observers_.RemoveObserver(observer);
}

void DisplayWatcher::OnUdevEvent(const UdevEvent& event) {
  VLOG(1) << "Got udev event for " << event.device_info.sysname
          << " on subsystem " << event.device_info.subsystem;
  UpdateDisplays(WITH_DEBOUNCE);
}

class DisplayWatcher::Update {
 public:
  Update(const base::FilePath& i2c_dev_path,
         const base::FilePath& sysfs_drm_path)
      : i2c_dev_path_(i2c_dev_path), sysfs_drm_path_(sysfs_drm_path) {}
  Update(const Update&) = delete;
  Update& operator=(const Update&) = delete;

  // Scans /sys for displays.
  std::vector<DisplayInfo> FindDisplays();

 private:
  // Returns the sys path of the parent device of the DRM device.
  base::FilePath GetSysPath(const base::FilePath& drm_dir);

  // Returns the path to the I2C device used for communicating with the display
  // connected to the device described by |drm_dir| under the device described
  // by |sys_dir|. Returns an empty path if the device isn't found.
  base::FilePath GetI2CDevicePath(const base::FilePath& drm_dir,
                                  const base::FilePath& sys_dir);

  // Helper used by GetI2CDevicePath() to locate the I2C device used for DDC/CI
  // communication. Returns an empty path if the device isn't found.
  base::FilePath FindI2CDeviceInDir(const base::FilePath& dir,
                                    std::string_view required_name = "");

  // Helper used by FindI2CDeviceInDir() to locate all potential I2C device used
  // for DDC/CI communication under a particular path.
  base::circular_deque<base::FilePath> EnumerateI2CDevicesInDir(
      const base::FilePath& dir, std::string_view required_name);

  // Owned by the DisplayWatcher owning |this|.
  const base::FilePath& i2c_dev_path_;
  const base::FilePath& sysfs_drm_path_;

  std::map<base::FilePath, base::circular_deque<base::FilePath>>
      i2c_devs_by_parent_;
};

base::FilePath DisplayWatcher::Update::GetSysPath(
    const base::FilePath& drm_dir) {
  base::FilePath sys_path(drm_dir);

  sys_path = sys_path.Append("device");  // symlink to the drm card
  sys_path = sys_path.Append("device");  // symlink to the parent device

  sys_path = base::MakeAbsoluteFilePath(sys_path);

  // EVDI devices have an additional symlink to their parent device.
  std::vector<std::string> components = sys_path.GetComponents();
  for (const auto& component : components) {
    if (base::StartsWith(component, "evdi", base::CompareCase::SENSITIVE) &&
        base::PathExists(sys_path.Append("device"))) {
      sys_path = sys_path.Append("device");
      sys_path = base::MakeAbsoluteFilePath(sys_path);
      break;
    }
  }

  return sys_path;
}

base::FilePath DisplayWatcher::Update::GetI2CDevicePath(
    const base::FilePath& drm_dir, const base::FilePath& sys_dir) {
  base::FilePath i2c_dev;

  i2c_dev = FindI2CDeviceInDir(drm_dir.AppendASCII("ddc/i2c-dev"));
  if (!base::PathExists(i2c_dev)) {
    i2c_dev = FindI2CDeviceInDir(drm_dir);
  }
  if (!base::PathExists(i2c_dev)) {
    i2c_dev = FindI2CDeviceInDir(sys_dir, "DPMST");
  }

  return i2c_dev;
}

base::FilePath DisplayWatcher::Update::FindI2CDeviceInDir(
    const base::FilePath& dir, std::string_view required_name) {
  auto [it, inserted] = i2c_devs_by_parent_.try_emplace(dir);
  auto& i2c_devs = it->second;
  if (inserted) {
    i2c_devs = EnumerateI2CDevicesInDir(dir, required_name);
  }
  base::FilePath i2c_dev;
  if (!i2c_devs.empty()) {
    i2c_dev = std::move(i2c_devs.front());
    i2c_devs.pop_front();
  }
  return i2c_dev;
}

base::circular_deque<base::FilePath>
DisplayWatcher::Update::EnumerateI2CDevicesInDir(
    const base::FilePath& dir, std::string_view required_name) {
  base::circular_deque<base::FilePath> i2c_devs;
  base::FileEnumerator enumerator(dir,
                                  false,  // recursive
                                  base::FileEnumerator::DIRECTORIES,
                                  kI2CDeviceNamePattern);
  for (base::FilePath i2c_dir = enumerator.Next(); !i2c_dir.empty();
       i2c_dir = enumerator.Next()) {
    const std::string i2c_name = i2c_dir.BaseName().value();
    base::FilePath i2c_dev = i2c_dev_path_.Append(i2c_name);
    if (!base::PathExists(i2c_dev)) {
      continue;
    }
    if (std::string name;
        !required_name.empty() &&
        (!base::ReadFileToString(i2c_dir.Append("name"), &name) ||
         base::TrimWhitespaceASCII(name, base::TRIM_ALL) != required_name)) {
      continue;
    }
    i2c_devs.push_back(i2c_dev);
  }
  std::sort(i2c_devs.begin(), i2c_devs.end());
  return i2c_devs;
}

void DisplayWatcher::PublishDisplayChanges() {
  for (DisplayWatcherObserver& observer : observers_) {
    observer.OnDisplaysChanged(displays_);
  }
}

void DisplayWatcher::HandleDebounceTimeout() {
  TRACE_EVENT("power", "DisplayWatcher::HandleDebounceTimeout");
  PublishDisplayChanges();
}

std::vector<DisplayInfo> DisplayWatcher::Update::FindDisplays() {
  std::vector<DisplayInfo> new_displays;

  base::FileEnumerator enumerator(
      sysfs_drm_path_,
      false,  // recursive
      static_cast<base::FileEnumerator::FileType>(
          base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS),
      kDrmDeviceNamePattern);
  for (base::FilePath device_path = enumerator.Next(); !device_path.empty();
       device_path = enumerator.Next()) {
    DisplayInfo info;
    if (IsConnectorStatus(device_path, kDrmStatusConnected)) {
      info.connector_status = DisplayInfo::ConnectorStatus::CONNECTED;
    } else if (IsConnectorStatus(device_path, kDrmStatusUnknown)) {
      info.connector_status = DisplayInfo::ConnectorStatus::UNKNOWN;
    } else {
      continue;
    }

    info.drm_path = device_path;
    info.sys_path = GetSysPath(device_path);
    info.i2c_path = GetI2CDevicePath(device_path, info.sys_path);
    new_displays.push_back(info);
    VLOG(1) << "Found connected display: drm_path=" << info.drm_path.value()
            << ", i2c_path=" << info.i2c_path.value()
            << ", sys_path=" << info.sys_path.value();
  }

  std::sort(new_displays.begin(), new_displays.end());

  return new_displays;
}

void DisplayWatcher::UpdateDisplays(UpdateMode update_mode) {
  std::vector<DisplayInfo> new_displays =
      Update(i2c_dev_path_, sysfs_drm_path_).FindDisplays();
  if (new_displays == displays_) {
    return;
  }

  displays_.swap(new_displays);

  if (update_mode == WITH_DEBOUNCE) {
    if (!debounce_timer_.IsRunning()) {
      // Advertise about display mode change after |kDebounceDelay| delay,
      // giving enough time for things to settle.
      debounce_timer_.Start(FROM_HERE, kDebounceDelay, this,
                            &DisplayWatcher::HandleDebounceTimeout);
    } else {
      // If the debounce timer is already running, avoid advertising about
      // display configuration change immediately. Instead reset the timer to
      // wait for things to settle.
      debounce_timer_.Reset();
    }
  } else {
    PublishDisplayChanges();
  }
}

}  // namespace power_manager::system
