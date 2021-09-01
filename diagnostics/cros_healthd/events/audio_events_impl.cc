// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/audio_events_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <cras/dbus-proxies.h>

namespace {

void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface << "." << signal;
    return;
  }
  VLOG(2) << "Successfully connected to D-Bus signal " << interface << "."
          << signal;
}

}  // namespace

namespace diagnostics {

AudioEventsImpl::AudioEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
  context_->cras_proxy()->RegisterUnderrunSignalHandler(
      base::BindRepeating(&AudioEventsImpl::OnUnderrunSignal,
                          base::Unretained(this)),
      base::BindOnce(&HandleSignalConnected));

  context_->cras_proxy()->RegisterSevereUnderrunSignalHandler(
      base::BindRepeating(&AudioEventsImpl::OnSevereUnderrunSignal,
                          base::Unretained(this)),
      base::BindOnce(&HandleSignalConnected));
}

void AudioEventsImpl::AddObserver(
    mojo::PendingRemote<chromeos::cros_healthd::mojom::CrosHealthdAudioObserver>
        observer) {
  observers_.Add(std::move(observer));
}

void AudioEventsImpl::OnUnderrunSignal() {
  for (auto& observer : observers_)
    observer->OnUnderrun();
}

void AudioEventsImpl::OnSevereUnderrunSignal() {
  for (auto& observer : observers_)
    observer->OnSevereUnderrun();
}

}  // namespace diagnostics
