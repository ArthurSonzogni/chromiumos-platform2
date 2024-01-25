// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

#include <algorithm>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/logging.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper_impl.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kDevInputPath[] = "/dev/input/";

mojom::NullableUint32Ptr FetchOptionalUnsignedSlotValue(LibevdevWrapper* dev,
                                                        unsigned int slot,
                                                        unsigned int code) {
  int out_value;
  if (dev->FetchSlotValue(slot, code, &out_value) && out_value >= 0) {
    return mojom::NullableUint32::New(out_value);
  }
  return nullptr;
}

}  // namespace

std::vector<mojom::TouchPointInfoPtr> FetchTouchPoints(LibevdevWrapper* dev) {
  int num_slot = dev->GetNumSlots();
  if (num_slot < 0) {
    LOG(ERROR) << "The evdev device does not provide any slots.";
    return {};
  }
  std::vector<mojom::TouchPointInfoPtr> points;
  for (int slot = 0; slot < num_slot; ++slot) {
    int value_x, value_y, id;
    if (dev->FetchSlotValue(slot, ABS_MT_POSITION_X, &value_x) &&
        dev->FetchSlotValue(slot, ABS_MT_POSITION_Y, &value_y) &&
        dev->FetchSlotValue(slot, ABS_MT_TRACKING_ID, &id)) {
      // A non-negative tracking id is interpreted as a contact, and the value
      // -1 denotes an unused slot.
      if (id >= 0 && value_x >= 0 && value_y >= 0) {
        auto point_info = mojom::TouchPointInfo::New();
        point_info->tracking_id = id;
        point_info->x = value_x;
        point_info->y = value_y;
        point_info->pressure =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_PRESSURE);
        point_info->touch_major =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_TOUCH_MAJOR);
        point_info->touch_minor =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_TOUCH_MINOR);
        points.push_back(std::move(point_info));
      }
    }
  }
  return points;
}

EvdevUtil::EvdevDevice::EvdevDevice(base::ScopedFD fd,
                                    std::unique_ptr<LibevdevWrapper> dev)
    : fd_(std::move(fd)), dev_(std::move(dev)) {}

EvdevUtil::EvdevDevice::~EvdevDevice() = default;

bool EvdevUtil::EvdevDevice::StarWatchingEvents(
    base::RepeatingCallback<void(LibevdevWrapper*)> on_evdev_event) {
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd_.get(), base::BindRepeating(on_evdev_event, dev_.get()));
  return !!watcher_;
}

EvdevUtil::EvdevUtil(std::unique_ptr<Delegate> delegate,
                     bool allow_multiple_devices)
    : EvdevUtil(std::move(delegate),
                allow_multiple_devices,
                base::BindRepeating(&LibevdevWrapperImpl::Create)) {}

EvdevUtil::EvdevUtil(std::unique_ptr<Delegate> delegate,
                     bool allow_multiple_devices,
                     LibevdevWrapperFactoryMethod factory_method)
    : allow_multiple_devices_(allow_multiple_devices),
      delegate_(std::move(delegate)) {
  Initialize(factory_method);
}

EvdevUtil::~EvdevUtil() = default;

void EvdevUtil::Initialize(LibevdevWrapperFactoryMethod factory_method) {
  base::FileEnumerator file_enum(GetRootedPath(kDevInputPath),
                                 /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
    if (Initialize(path, factory_method) && !allow_multiple_devices_) {
      return;
    }
  }

  if (devs_.empty()) {
    LOG(ERROR) << "EvdevUtil can't find target, initialization fail";
    delegate_->InitializationFail(/*custom_reason = */ 0,
                                  "EvdevUtil can't find target.");
  }
}

bool EvdevUtil::Initialize(const base::FilePath& path,
                           LibevdevWrapperFactoryMethod factory_method) {
  auto fd = base::ScopedFD(open(path.value().c_str(), O_RDONLY | O_NONBLOCK));
  if (!fd.is_valid()) {
    return false;
  }

  auto dev = factory_method.Run(fd.get());
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
          &EvdevUtil::OnEvdevEvent, base::Unretained(this)))) {
    LOG(ERROR) << "Fail to monitor evdev node: " << path;
    return false;
  }

  devs_.push_back(std::move(evdev_device));

  LOG(INFO) << "Connected to evdev node: " << path
            << ", device name: " << libevdev_ptr->GetName();
  delegate_->ReportProperties(libevdev_ptr);
  return true;
}

void EvdevUtil::OnEvdevEvent(LibevdevWrapper* dev) {
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

VolumeButtonEvdevDelegate::VolumeButtonEvdevDelegate(
    mojo::PendingRemote<mojom::VolumeButtonObserver> observer)
    : observer_(std::move(observer)) {}

bool VolumeButtonEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  return dev->HasEventCode(EV_KEY, KEY_VOLUMEDOWN) &&
         dev->HasEventCode(EV_KEY, KEY_VOLUMEUP);
}

void VolumeButtonEvdevDelegate::FireEvent(const input_event& ev,
                                          LibevdevWrapper* dev) {
  if (ev.type != EV_KEY) {
    return;
  }

  mojom::VolumeButtonObserver::Button button;
  if (ev.code == KEY_VOLUMEUP) {
    button = mojom::VolumeButtonObserver::Button::kVolumeUp;
  } else if (ev.code == KEY_VOLUMEDOWN) {
    button = mojom::VolumeButtonObserver::Button::kVolumeDown;
  } else {
    return;
  }

  mojom::VolumeButtonObserver::ButtonState button_state;
  if (ev.value == 0) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kUp;
  } else if (ev.value == 1) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kDown;
  } else if (ev.value == 2) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kRepeat;
  } else {
    return;
  }

  observer_->OnEvent(button, button_state);
}

void VolumeButtonEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void VolumeButtonEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {}

}  // namespace diagnostics
