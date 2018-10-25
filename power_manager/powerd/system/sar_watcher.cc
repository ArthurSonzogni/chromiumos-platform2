// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/sar_watcher.h"

#include <fcntl.h>
#include <linux/iio/events.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/posix/eintr_wrapper.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/powerd/system/sar_observer.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace system {

namespace {

int OpenIioFd(const base::FilePath& path) {
  const std::string file(path.value());
  int fd = open(file.c_str(), O_RDONLY);
  if (fd == -1) {
    PLOG(WARNING) << "Unable to open file " << file;
    return -1;
  }

  int event_fd = -1;
  int ret = ioctl(fd, IIO_GET_EVENT_FD_IOCTL, &event_fd);
  close(fd);

  if (ret < 0 || event_fd == -1) {
    PLOG(WARNING) << "Unable to open event descriptor for file " << file;
    return -1;
  }

  return event_fd;
}

// Returns which subsystems the sensor at |path| can provide proximity
// data for. The return value is a bitwise combination of SensorRole values.
uint32_t GetSensorRole(const std::string& path) {
  uint32_t responsibility = SarWatcher::SensorRole::SENSOR_ROLE_NONE;

  const auto proximity_index = path.find("proximity-");
  if (proximity_index == std::string::npos)
    return responsibility;

  if (path.find("-lte", proximity_index) != std::string::npos)
    responsibility |= SarWatcher::SensorRole::SENSOR_ROLE_LTE;
  if (path.find("-wifi", proximity_index) != std::string::npos)
    responsibility |= SarWatcher::SensorRole::SENSOR_ROLE_WIFI;

  return responsibility;
}

}  // namespace

const char SarWatcher::kIioUdevSubsystem[] = "iio";

const char SarWatcher::kIioUdevDevice[] = "iio_device";

void SarWatcher::set_open_iio_events_func_for_testing(OpenIioEventsFunc f) {
  open_iio_events_func_ = f;
}

SarWatcher::SarWatcher() : open_iio_events_func_(base::Bind(&OpenIioFd)) {}

SarWatcher::~SarWatcher() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kIioUdevSubsystem, this);
}

bool SarWatcher::Init(PrefsInterface* prefs, UdevInterface* udev) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kIioUdevSubsystem, this);

  std::vector<UdevDeviceInfo> iio_devices;
  if (!udev_->GetSubsystemDevices(kIioUdevSubsystem, &iio_devices)) {
    LOG(ERROR) << "Enumeration of existing proximity devices failed.";
    return false;
  }

  for (auto const& iio_dev : iio_devices) {
    std::string devlink;
    if (!IsIioProximitySensor(iio_dev, &devlink))
      continue;
    if (!OnSensorDetected(iio_dev.syspath, devlink)) {
      LOG(ERROR) << "Unable to set up proximity sensor " << iio_dev.syspath;
    }
  }

  return true;
}

void SarWatcher::AddObserver(SarObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void SarWatcher::RemoveObserver(SarObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void SarWatcher::OnUdevEvent(const UdevEvent& event) {
  if (event.action != UdevEvent::Action::ADD)
    return;

  std::string devlink;
  if (!IsIioProximitySensor(event.device_info, &devlink))
    return;

  if (!OnSensorDetected(event.device_info.syspath, devlink))
    LOG(ERROR) << "Unable to setup proximity sensor "
               << event.device_info.syspath;
}

void SarWatcher::OnFileCanReadWithoutBlocking(int fd) {
  if (sensors_.find(fd) == sensors_.end()) {
    LOG(WARNING) << "Notified about FD " << fd << "which is not a sensor";
    return;
  }

  // TODO(egranata): abstract IIO event functionality
  static constexpr size_t iio_event_size = 16;
  uint8_t iio_event_buf[iio_event_size] = {0};
  if (read(fd, &iio_event_buf[0], iio_event_size) == -1) {
    PLOG(ERROR) << "Failed to read from FD " << fd;
  }

  UserProximity proximity = UserProximity::UNKNOWN;
  switch (iio_event_buf[6]) {
    case 1:
      proximity = UserProximity::FAR;
      break;
    case 2:
      proximity = UserProximity::NEAR;
      break;
    default:
      LOG(ERROR) << "Unknown proximity value " << iio_event_buf[6];
      return;
  }

  for (auto& observer : observers_) {
    observer.OnProximityEvent(fd, proximity);
  }
}

bool SarWatcher::IsIioProximitySensor(const UdevDeviceInfo& dev,
                                      std::string* devlink_out) {
  DCHECK(udev_);

  if (dev.subsystem != kIioUdevSubsystem || dev.devtype != kIioUdevDevice)
    return false;

  std::vector<std::string> devlinks;
  const bool found_devlinks = udev_->GetDevlinks(dev.syspath, &devlinks);
  if (!found_devlinks) {
    LOG(WARNING) << "udev unable to discover devlinks for " << dev.syspath;
    return false;
  }

  for (const auto& dl : devlinks) {
    if (dl.find("proximity-") != std::string::npos) {
      *devlink_out = dl;
      return true;
    }
  }

  return false;
}

bool SarWatcher::OnSensorDetected(const std::string& syspath,
                                  const std::string& devlink) {
  using MLIO = base::MessageLoopForIO;

  uint32_t role = GetSensorRole(devlink);

  int event_fd = open_iio_events_func_.Run(base::FilePath(devlink));
  if (event_fd == -1) {
    LOG(WARNING) << "Unable to open event descriptor for file " << devlink;
    return false;
  }

  SensorInfo info;
  info.syspath = syspath;
  info.devlink = devlink;
  info.event_fd = event_fd;
  info.role = role;
  info.watcher = std::make_unique<MLIO::FileDescriptorWatcher>(FROM_HERE);

  const bool is_watching = MLIO::current()->WatchFileDescriptor(
      event_fd, true, MLIO::WATCH_READ, info.watcher.get(), this);
  if (!is_watching) {
    LOG(WARNING) << "Unable to watch event descriptor for file " << devlink;
    close(event_fd);
    return false;
  }

  sensors_.emplace(event_fd, std::move(info));

  for (auto& observer : observers_) {
    observer.OnNewSensor(event_fd, role);
  }

  return true;
}

}  // namespace system
}  // namespace power_manager
