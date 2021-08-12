// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event_subscriber.h"

#include <utility>

#include <base/check.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

}  // namespace

EventSubscriber::EventSubscriber()
    : mojo_adapter_(CrosHealthdMojoAdapter::Create()) {
  DCHECK(mojo_adapter_);
}

EventSubscriber::~EventSubscriber() = default;

bool EventSubscriber::SubscribeToBluetoothEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdBluetoothObserver> remote;
  bluetooth_subscriber_ = std::make_unique<BluetoothSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddBluetoothObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToLidEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdLidObserver> remote;
  lid_subscriber_ =
      std::make_unique<LidSubscriber>(remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddLidObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToPowerEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdPowerObserver> remote;
  power_subscriber_ = std::make_unique<PowerSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddPowerObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToNetworkEvents() {
  mojo::PendingRemote<network_health_ipc::NetworkEventsObserver> remote;
  network_subscriber_ = std::make_unique<NetworkSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddNetworkObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToAudioEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdAudioObserver> remote;
  audio_subscriber_ = std::make_unique<AudioSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddAudioObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToThunderboltEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdThunderboltObserver> remote;
  thunderbolt_subscriber_ = std::make_unique<ThunderboltSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddThunderboltObserver(std::move(remote));
}

bool EventSubscriber::SubscribeToUsbEvents() {
  mojo::PendingRemote<mojo_ipc::CrosHealthdUsbObserver> remote;
  usb_subscriber_ =
      std::make_unique<UsbSubscriber>(remote.InitWithNewPipeAndPassReceiver());
  return mojo_adapter_->AddUsbObserver(std::move(remote));
}

}  // namespace diagnostics
