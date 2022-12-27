// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/service_constants.h>

#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

std::string EnumToString(mojom::UsbEventInfo::State state) {
  switch (state) {
    case mojom::UsbEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::UsbEventInfo::State::kAdd:
      return "Add";
    case mojom::UsbEventInfo::State::kRemove:
      return "Remove";
  }
}

std::string EnumToString(mojom::ThunderboltEventInfo::State state) {
  switch (state) {
    case mojom::ThunderboltEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::ThunderboltEventInfo::State::kAdd:
      return "Device added";
    case mojom::ThunderboltEventInfo::State::kRemove:
      return "Device removed";
    case mojom::ThunderboltEventInfo::State::kAuthorized:
      return "Device Authorized";
    case mojom::ThunderboltEventInfo::State::kUnAuthorized:
      return "Device UnAuthorized";
  }
}

void OutputUsbEventInfo(const mojom::UsbEventInfoPtr& info) {
  base::Value output{base::Value::Type::DICTIONARY};

  output.SetStringKey("event", EnumToString(info->state));
  output.SetStringKey("vendor", info->vendor);
  output.SetStringKey("name", info->name);
  output.SetKey("vid", base::Value(info->vid));
  output.SetKey("pid", base::Value(info->pid));

  auto* categories =
      output.SetKey("categories", base::Value{base::Value::Type::LIST});
  for (const auto& category : info->categories) {
    categories->Append(category);
  }

  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);

  std::cout << json << std::endl;
}

void OutputThunderboltEventInfo(const mojom::ThunderboltEventInfoPtr& info) {
  std::cout << "Thunderbolt event received: " << EnumToString(info->state)
            << std::endl;
}

}  // namespace

EventSubscriber::EventSubscriber() {
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kCrosHealthdEvent, event_service_);
}

EventSubscriber::~EventSubscriber() = default;

void EventSubscriber::SubscribeToBluetoothEvents() {
  mojo::PendingRemote<mojom::CrosHealthdBluetoothObserver> remote;
  bluetooth_subscriber_ = std::make_unique<BluetoothSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddBluetoothObserver(std::move(remote));
}

void EventSubscriber::SubscribeToLidEvents() {
  mojo::PendingRemote<mojom::CrosHealthdLidObserver> remote;
  lid_subscriber_ =
      std::make_unique<LidSubscriber>(remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddLidObserver(std::move(remote));
}

void EventSubscriber::SubscribeToPowerEvents() {
  mojo::PendingRemote<mojom::CrosHealthdPowerObserver> remote;
  power_subscriber_ = std::make_unique<PowerSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddPowerObserver(std::move(remote));
}

void EventSubscriber::SubscribeToNetworkEvents() {
  mojo::PendingRemote<network_health_ipc::NetworkEventsObserver> remote;
  network_subscriber_ = std::make_unique<NetworkSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddNetworkObserver(std::move(remote));
}

void EventSubscriber::SubscribeToAudioEvents() {
  mojo::PendingRemote<mojom::CrosHealthdAudioObserver> remote;
  audio_subscriber_ = std::make_unique<AudioSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddAudioObserver(std::move(remote));
}

void EventSubscriber::SubscribeToEvents(mojom::EventCategoryEnum category) {
  event_service_->AddEventObserver(category,
                                   receiver_.BindNewPipeAndPassRemote());
}

void EventSubscriber::OnEvent(const mojom::EventInfoPtr info) {
  switch (info->which()) {
    case mojom::EventInfo::Tag::kDefaultType:
      LOG(FATAL) << "Got UnmappedEnumField";
      break;
    case mojom::EventInfo::Tag::kUsbEventInfo:
      OutputUsbEventInfo(info->get_usb_event_info());
      break;
    case mojom::EventInfo::Tag::kThunderboltEventInfo:
      OutputThunderboltEventInfo(info->get_thunderbolt_event_info());
      break;
    case mojom::EventInfo::Tag::kLidEventInfo:
      NOTIMPLEMENTED();
      break;
    case mojom::EventInfo::Tag::kBluetoothEventInfo:
      NOTIMPLEMENTED();
      break;
    case mojom::EventInfo::Tag::kPowerEventInfo:
      NOTIMPLEMENTED();
      break;
    case mojom::EventInfo::Tag::kAudioEventInfo:
      NOTIMPLEMENTED();
      break;
  }
}

}  // namespace diagnostics
