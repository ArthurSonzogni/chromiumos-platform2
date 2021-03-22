// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/lid_events_impl.h"

#include <utility>

#include <base/check.h>
#include <base/logging.h>

namespace diagnostics {

LidEventsImpl::LidEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
}

LidEventsImpl::~LidEventsImpl() {
  if (is_observing_powerd_)
    context_->powerd_adapter()->RemoveLidObserver(this);
}

void LidEventsImpl::AddObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
        observer) {
  if (!is_observing_powerd_) {
    context_->powerd_adapter()->AddLidObserver(this);
    is_observing_powerd_ = true;
  }
  observers_.Add(std::move(observer));
}

void LidEventsImpl::OnLidClosedSignal() {
  for (auto& observer : observers_)
    observer->OnLidClosed();

  StopObservingPowerdIfNecessary();
}

void LidEventsImpl::OnLidOpenedSignal() {
  for (auto& observer : observers_)
    observer->OnLidOpened();

  StopObservingPowerdIfNecessary();
}

void LidEventsImpl::StopObservingPowerdIfNecessary() {
  if (!observers_.empty())
    return;

  context_->powerd_adapter()->RemoveLidObserver(this);
  is_observing_powerd_ = false;
}

}  // namespace diagnostics
