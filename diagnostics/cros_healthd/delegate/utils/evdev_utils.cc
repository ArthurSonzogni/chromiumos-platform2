// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

#include <fcntl.h>
#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/logging.h>

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kDevInputPath[] = "/dev/input/";

}  // namespace

EvdevUtil::EvdevUtil(Delegate* delegate) : delegate_(delegate) {
  Initialize();
}

EvdevUtil::~EvdevUtil() = default;

void EvdevUtil::Initialize() {
  base::FileEnumerator file_enum(base::FilePath(kDevInputPath),
                                 /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
    if (Initialize(path)) {
      return;
    }
  }

  LOG(ERROR) << "EvdevUtil can't find target, initialization fail";
  delegate_->InitializationFail();
}

bool EvdevUtil::Initialize(const base::FilePath& path) {
  auto fd = base::ScopedFD(open(path.value().c_str(), O_RDONLY | O_NONBLOCK));
  if (!fd.is_valid()) {
    return false;
  }

  ScopedLibevdev dev(libevdev_new());
  int rc = libevdev_set_fd(dev.get(), fd.get());
  if (rc < 0) {
    return false;
  }

  if (!delegate_->IsTarget(dev.get())) {
    return false;
  }

  dev_ = std::move(dev);
  fd_ = std::move(fd);
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(),
      base::BindRepeating(&EvdevUtil::OnEvdevEvent, base::Unretained(this)));

  if (!watcher_) {
    LOG(ERROR) << "Fail to monitor evdev node: " << path;
    dev_.reset();
    fd_.reset();
    return false;
  }

  delegate_->ReportProperties(dev_.get());
  return true;
}

void EvdevUtil::OnEvdevEvent() {
  input_event ev;
  int rc = 0;

  do {
    rc = libevdev_next_event(
        dev_.get(), LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
        &ev);
    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      delegate_->FireEvent(ev, dev_.get());
    }
  } while (rc == LIBEVDEV_READ_STATUS_SUCCESS ||
           rc == LIBEVDEV_READ_STATUS_SYNC);
}

EvdevAudioJackObserver::EvdevAudioJackObserver(
    mojo::PendingRemote<mojom::AudioJackObserver> observer)
    : observer_(std::move(observer)) {}

bool EvdevAudioJackObserver::IsTarget(libevdev* dev) {
  return libevdev_has_event_code(dev, EV_SW, SW_HEADPHONE_INSERT) &&
         libevdev_has_event_code(dev, EV_SW, SW_MICROPHONE_INSERT);
}

void EvdevAudioJackObserver::FireEvent(const input_event& ev, libevdev* dev) {
  if (ev.type != EV_SW) {
    return;
  }

  if (ev.code == SW_HEADPHONE_INSERT || ev.code == SW_MICROPHONE_INSERT) {
    if (ev.value == 1) {
      observer_->OnAdd();
    } else {
      observer_->OnRemove();
    }
  }
}

void EvdevAudioJackObserver::InitializationFail() {
  observer_.reset();
}

void EvdevAudioJackObserver::ReportProperties(libevdev* dev) {}

}  // namespace diagnostics
