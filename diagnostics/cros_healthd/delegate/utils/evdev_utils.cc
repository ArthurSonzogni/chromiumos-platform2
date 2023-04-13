// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

#include <algorithm>
#include <fcntl.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/logging.h>

#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr char kDevInputPath[] = "/dev/input/";

std::optional<mojom::InputTouchButton> EventCodeToInputTouchButton(
    unsigned int code) {
  switch (code) {
    case BTN_LEFT:
      return mojom::InputTouchButton::kLeft;
    case BTN_MIDDLE:
      return mojom::InputTouchButton::kMiddle;
    case BTN_RIGHT:
      return mojom::InputTouchButton::kRight;
    default:
      return std::nullopt;
  }
}

mojom::NullableUint32Ptr FetchOptionalUnsignedSlotValue(const libevdev* dev,
                                                        unsigned int slot,
                                                        unsigned int code) {
  int out_value;
  if (libevdev_fetch_slot_value(dev, slot, code, &out_value) &&
      out_value >= 0) {
    return mojom::NullableUint32::New(out_value);
  }
  return nullptr;
}

std::vector<mojom::TouchPointInfoPtr> FetchTouchPoints(const libevdev* dev) {
  int num_slot = libevdev_get_num_slots(dev);
  if (num_slot < 0) {
    LOG(ERROR) << "The evdev device does not provide any slots.";
    return {};
  }
  std::vector<mojom::TouchPointInfoPtr> points;
  for (int slot = 0; slot < num_slot; ++slot) {
    int value_x, value_y, id;
    if (libevdev_fetch_slot_value(dev, slot, ABS_MT_POSITION_X, &value_x) &&
        libevdev_fetch_slot_value(dev, slot, ABS_MT_POSITION_Y, &value_y) &&
        libevdev_fetch_slot_value(dev, slot, ABS_MT_TRACKING_ID, &id)) {
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
  delegate_->InitializationFail(/*custom_reason = */ 0,
                                "EvdevUtil can't find target.");
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

  LOG(INFO) << "Connected to evdev node: " << path
            << ", device name: " << libevdev_get_name(dev_.get());
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

  if (ev.value == 1) {
    if (ev.code == SW_HEADPHONE_INSERT) {
      observer_->OnAdd(mojom::AudioJackEventInfo::DeviceType::kHeadphone);
    }
    if (ev.code == SW_MICROPHONE_INSERT) {
      observer_->OnAdd(mojom::AudioJackEventInfo::DeviceType::kMicrophone);
    }
  } else {
    if (ev.code == SW_HEADPHONE_INSERT) {
      observer_->OnRemove(mojom::AudioJackEventInfo::DeviceType::kHeadphone);
    }
    if (ev.code == SW_MICROPHONE_INSERT) {
      observer_->OnRemove(mojom::AudioJackEventInfo::DeviceType::kMicrophone);
    }
  }
}

void EvdevAudioJackObserver::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void EvdevAudioJackObserver::ReportProperties(libevdev* dev) {}

EvdevTouchpadObserver::EvdevTouchpadObserver(
    mojo::PendingRemote<mojom::TouchpadObserver> observer)
    : observer_(std::move(observer)) {}

bool EvdevTouchpadObserver::IsTarget(libevdev* dev) {
  // - Typical pointer devices: touchpads, tablets, mice.
  // - Typical non-direct devices: touchpads, mice.
  // - Check for event type EV_ABS to exclude mice, which report movement with
  //   REL_{X,Y} instead of ABS_{X,Y}.
  return libevdev_has_property(dev, INPUT_PROP_POINTER) &&
         !libevdev_has_property(dev, INPUT_PROP_DIRECT) &&
         libevdev_has_event_type(dev, EV_ABS);
}

void EvdevTouchpadObserver::FireEvent(const input_event& ev, libevdev* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    observer_->OnTouch(mojom::TouchpadTouchEvent::New(FetchTouchPoints(dev)));
  } else if (ev.type == EV_KEY) {
    auto button = EventCodeToInputTouchButton(ev.code);
    if (button.has_value()) {
      bool pressed = (ev.value != 0);
      observer_->OnButton(
          mojom::TouchpadButtonEvent::New(button.value(), pressed));
    }
  }
}

void EvdevTouchpadObserver::InitializationFail(uint32_t custom_reason,
                                               const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void EvdevTouchpadObserver::ReportProperties(libevdev* dev) {
  auto connected_event = mojom::TouchpadConnectedEvent::New();
  connected_event->max_x = std::max(libevdev_get_abs_maximum(dev, ABS_X), 0);
  connected_event->max_y = std::max(libevdev_get_abs_maximum(dev, ABS_Y), 0);
  connected_event->max_pressure =
      std::max(libevdev_get_abs_maximum(dev, ABS_MT_PRESSURE), 0);
  if (libevdev_has_event_type(dev, EV_KEY)) {
    std::vector<unsigned int> codes{BTN_LEFT, BTN_MIDDLE, BTN_RIGHT};
    for (const auto code : codes) {
      if (libevdev_has_event_code(dev, EV_KEY, code)) {
        auto button = EventCodeToInputTouchButton(code);
        if (button.has_value()) {
          connected_event->buttons.push_back(button.value());
        }
      }
    }
  }
  observer_->OnConnected(std::move(connected_event));
}

EvdevTouchscreenObserver::EvdevTouchscreenObserver(
    mojo::PendingRemote<mojom::TouchscreenObserver> observer)
    : observer_(std::move(observer)) {}

bool EvdevTouchscreenObserver::IsTarget(libevdev* dev) {
  // - Typical non-pointer devices: touchscreens.
  // - Typical direct devices: touchscreens, drawing tablets.
  // - Use ABS_MT_TRACKING_ID to filter out stylus.
  return !libevdev_has_property(dev, INPUT_PROP_POINTER) &&
         libevdev_has_property(dev, INPUT_PROP_DIRECT) &&
         libevdev_has_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID);
}

void EvdevTouchscreenObserver::FireEvent(const input_event& ev, libevdev* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    observer_->OnTouch(
        mojom::TouchscreenTouchEvent::New(FetchTouchPoints(dev)));
  }
}

void EvdevTouchscreenObserver::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void EvdevTouchscreenObserver::ReportProperties(libevdev* dev) {
  auto connected_event = mojom::TouchscreenConnectedEvent::New();
  connected_event->max_x = std::max(libevdev_get_abs_maximum(dev, ABS_X), 0);
  connected_event->max_y = std::max(libevdev_get_abs_maximum(dev, ABS_Y), 0);
  connected_event->max_pressure =
      std::max(libevdev_get_abs_maximum(dev, ABS_MT_PRESSURE), 0);
  observer_->OnConnected(std::move(connected_event));
}

EvdevStylusGarageObserver::EvdevStylusGarageObserver(
    mojo::PendingRemote<mojom::StylusGarageObserver> observer)
    : observer_(std::move(observer)) {}

bool EvdevStylusGarageObserver::IsTarget(libevdev* dev) {
  return libevdev_has_event_code(dev, EV_SW, SW_PEN_INSERTED);
}

void EvdevStylusGarageObserver::FireEvent(const input_event& ev,
                                          libevdev* dev) {
  if (ev.type != EV_SW) {
    return;
  }

  if (ev.code == SW_PEN_INSERTED) {
    if (ev.value == 1) {
      observer_->OnInsert();
    } else {
      observer_->OnRemove();
    }
  }
}

void EvdevStylusGarageObserver::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void EvdevStylusGarageObserver::ReportProperties(libevdev* dev) {}

EvdevStylusObserver::EvdevStylusObserver(
    mojo::PendingRemote<mojom::StylusObserver> observer)
    : observer_(std::move(observer)) {}

bool EvdevStylusObserver::IsTarget(libevdev* dev) {
  // - Typical non-pointer devices: touchscreens.
  // - Typical direct devices: touchscreens, drawing tablets.
  // - Use ABS_MT_TRACKING_ID to filter out touchscreen.
  return !libevdev_has_property(dev, INPUT_PROP_POINTER) &&
         libevdev_has_property(dev, INPUT_PROP_DIRECT) &&
         !libevdev_has_event_code(dev, EV_ABS, ABS_MT_TRACKING_ID);
}

void EvdevStylusObserver::FireEvent(const input_event& ev, libevdev* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    bool is_stylus_in_contact =
        libevdev_get_event_value(dev, EV_KEY, BTN_TOUCH);
    if (is_stylus_in_contact) {
      auto point_info = mojom::StylusTouchPointInfo::New();
      point_info->x = libevdev_get_event_value(dev, EV_ABS, ABS_X);
      point_info->y = libevdev_get_event_value(dev, EV_ABS, ABS_Y);
      point_info->pressure = mojom::NullableUint32::New(
          libevdev_get_event_value(dev, EV_ABS, ABS_PRESSURE));

      observer_->OnTouch(mojom::StylusTouchEvent::New(std::move(point_info)));
      last_event_has_touch_point_ = true;
    } else {
      // Don't repeatedly report events without the touch point.
      if (last_event_has_touch_point_) {
        observer_->OnTouch(mojom::StylusTouchEvent::New());
        last_event_has_touch_point_ = false;
      }
    }
  }
}

void EvdevStylusObserver::InitializationFail(uint32_t custom_reason,
                                             const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void EvdevStylusObserver::ReportProperties(libevdev* dev) {
  auto connected_event = mojom::StylusConnectedEvent::New();
  connected_event->max_x = std::max(libevdev_get_abs_maximum(dev, ABS_X), 0);
  connected_event->max_y = std::max(libevdev_get_abs_maximum(dev, ABS_Y), 0);
  connected_event->max_pressure =
      std::max(libevdev_get_abs_maximum(dev, ABS_PRESSURE), 0);
  observer_->OnConnected(std::move(connected_event));
}

}  // namespace diagnostics
