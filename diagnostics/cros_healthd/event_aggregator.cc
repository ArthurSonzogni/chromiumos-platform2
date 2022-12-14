// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/event_aggregator.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

EventAggregator::EventAggregator(Context* context) : context_(context) {}

EventAggregator::~EventAggregator() = default;

void EventAggregator::AddObserver(
    mojom::EventCategoryEnum category,
    mojo::PendingRemote<mojom::EventObserver> observer) {
  switch (category) {
    case mojom::EventCategoryEnum::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      break;
    case mojom::EventCategoryEnum::kUsb:
      NOTIMPLEMENTED();
      break;
    case mojom::EventCategoryEnum::kThunderbolt:
      NOTIMPLEMENTED();
      break;
    case mojom::EventCategoryEnum::kLid:
      NOTIMPLEMENTED();
      break;
    case mojom::EventCategoryEnum::kBluetooth:
      NOTIMPLEMENTED();
      break;
    case mojom::EventCategoryEnum::kPower:
      NOTIMPLEMENTED();
      break;
    case mojom::EventCategoryEnum::kAudio:
      NOTIMPLEMENTED();
      break;
  }
}

}  // namespace diagnostics
