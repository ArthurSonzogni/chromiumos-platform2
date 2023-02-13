// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "diagnostics/cros_healthd/event_aggregator.h"
#include "diagnostics/cros_healthd/events/audio_events_impl.h"
#include "diagnostics/cros_healthd/events/audio_jack_events_impl.h"
#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/events/lid_events_impl.h"
#include "diagnostics/cros_healthd/events/power_events_impl.h"
#include "diagnostics/cros_healthd/events/udev_events_impl.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

EventAggregator::EventAggregator(Context* context) : context_(context) {
  udev_events_ = std::make_unique<UdevEventsImpl>(context_);
  if (!udev_events_->Initialize()) {
    LOG(ERROR) << "Failed to initialize udev_events.";
  }
  lid_events_ = std::make_unique<LidEventsImpl>(context_);
  audio_jack_events_ = std::make_unique<AudioJackEventsImpl>(context_);
  power_events_ = std::make_unique<PowerEventsImpl>(context_);
  audio_events_ = std::make_unique<AudioEventsImpl>(context_);
  bluetooth_events_ = std::make_unique<BluetoothEventsImpl>(context_);
}

EventAggregator::~EventAggregator() = default;

void EventAggregator::AddObserver(
    mojom::EventCategoryEnum category,
    mojo::PendingRemote<mojom::EventObserver> observer) {
  switch (category) {
    case mojom::EventCategoryEnum::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      break;
    case mojom::EventCategoryEnum::kUsb:
      udev_events_->AddUsbObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kThunderbolt:
      udev_events_->AddThunderboltObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kLid:
      lid_events_->AddObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kBluetooth:
      bluetooth_events_->AddObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kPower:
      power_events_->AddObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kAudio:
      audio_events_->AddObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kAudioJack:
      audio_jack_events_->AddObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kSdCard:
      udev_events_->AddSdCardObserver(std::move(observer));
      break;
    case mojom::EventCategoryEnum::kNetwork:
      NOTIMPLEMENTED();
      break;
  }
}

void EventAggregator::AddObserver(
    mojo::PendingRemote<mojom::CrosHealthdUsbObserver> observer) {
  udev_events_->AddUsbObserver(std::move(observer));
}

void EventAggregator::AddObserver(
    mojo::PendingRemote<mojom::CrosHealthdThunderboltObserver> observer) {
  udev_events_->AddThunderboltObserver(std::move(observer));
}

void EventAggregator::AddObserver(
    mojo::PendingRemote<mojom::CrosHealthdPowerObserver> observer) {
  power_events_->AddObserver(std::move(observer));
}

void EventAggregator::AddObserver(
    mojo::PendingRemote<mojom::CrosHealthdAudioObserver> observer) {
  audio_events_->AddObserver(std::move(observer));
}

}  // namespace diagnostics
