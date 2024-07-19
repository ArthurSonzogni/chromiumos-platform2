// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_monitor.h"

#include <fcntl.h>

#include <utility>

#include <base/files/file_enumerator.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper_impl.h"

namespace diagnostics {

namespace {

constexpr char kDevInputPath[] = "/dev/input/";

}  // namespace

EvdevMonitor::EvdevDevice::EvdevDevice(base::ScopedFD fd,
                                       std::unique_ptr<LibevdevWrapper> dev)
    : fd_(std::move(fd)), dev_(std::move(dev)) {}

EvdevMonitor::EvdevDevice::~EvdevDevice() = default;

bool EvdevMonitor::EvdevDevice::StarWatchingEvents(
    base::RepeatingCallback<void(LibevdevWrapper*)> on_evdev_event) {
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(), base::BindRepeating(on_evdev_event, dev_.get()));
  return !!watcher_;
}

EvdevMonitor::EvdevMonitor(std::unique_ptr<Delegate> delegate)
    : delegate_(std::move(delegate)) {}

EvdevMonitor::~EvdevMonitor() = default;

std::unique_ptr<LibevdevWrapper> EvdevMonitor::CreateLibevdev(int fd) {
  return LibevdevWrapperImpl::Create(fd);
}

void EvdevMonitor::StartMonitoring(bool allow_multiple_devices) {
  base::FileEnumerator file_enum(GetRootedPath(kDevInputPath),
                                 /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
    if (TryMonitoringEvdevDevice(path) && !allow_multiple_devices) {
      return;
    }
  }

  if (devs_.empty()) {
    LOG(ERROR) << "EvdevMonitor can't find target, initialization fail";
    delegate_->InitializationFail(/*custom_reason = */ 0,
                                  "EvdevMonitor can't find target.");
  }
}

bool EvdevMonitor::TryMonitoringEvdevDevice(const base::FilePath& path) {
  auto fd = base::ScopedFD(open(path.value().c_str(), O_RDONLY | O_NONBLOCK));
  if (!fd.is_valid()) {
    return false;
  }

  auto dev = CreateLibevdev(fd.get());
  if (!dev) {
    return false;
  }

  if (!delegate_->IsTarget(dev.get())) {
    return false;
  }

  LibevdevWrapper* const libevdev_ptr = dev.get();

  auto evdev_device =
      std::make_unique<EvdevDevice>(std::move(fd), std::move(dev));
  if (!evdev_device->StarWatchingEvents(base::BindRepeating(
          &EvdevMonitor::OnEvdevEvent, base::Unretained(this)))) {
    LOG(ERROR) << "Fail to monitor evdev node: " << path;
    return false;
  }

  devs_.push_back(std::move(evdev_device));

  LOG(INFO) << "Connected to evdev node: " << path
            << ", device name: " << libevdev_ptr->GetName();
  delegate_->ReportProperties(libevdev_ptr);
  return true;
}

void EvdevMonitor::OnEvdevEvent(LibevdevWrapper* dev) {
  input_event ev;
  int rc = 0;

  do {
    rc = dev->NextEvent(LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
                        &ev);
    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      delegate_->FireEvent(ev, dev);
    }
  } while (rc == LIBEVDEV_READ_STATUS_SUCCESS ||
           rc == LIBEVDEV_READ_STATUS_SYNC);
}

}  // namespace diagnostics
