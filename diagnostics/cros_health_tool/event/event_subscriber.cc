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

std::string EnumToString(mojom::LidEventInfo::State state) {
  switch (state) {
    case mojom::LidEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::LidEventInfo::State::kClosed:
      return "Lid closed";
    case mojom::LidEventInfo::State::kOpened:
      return "Lid opened";
  }
}

std::string EnumToString(mojom::AudioJackEventInfo::State state) {
  switch (state) {
    case mojom::AudioJackEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::AudioJackEventInfo::State::kAdd:
      return "Add";
    case mojom::AudioJackEventInfo::State::kRemove:
      return "Remove";
  }
}

std::string EnumToString(mojom::SdCardEventInfo::State state) {
  switch (state) {
    case mojom::SdCardEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::SdCardEventInfo::State::kAdd:
      return "Sd Card added";
    case mojom::SdCardEventInfo::State::kRemove:
      return "Sd Card removed";
  }
}

std::string EnumToString(mojom::PowerEventInfo::State state) {
  switch (state) {
    case mojom::PowerEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::PowerEventInfo::State::kAcInserted:
      return "Ac inserted";
    case mojom::PowerEventInfo::State::kAcRemoved:
      return "Ac removed";
    case mojom::PowerEventInfo::State::kOsSuspend:
      return "OS suspend";
    case mojom::PowerEventInfo::State::kOsResume:
      return "OS resume";
  }
}

std::string EnumToString(mojom::AudioEventInfo::State state) {
  switch (state) {
    case mojom::AudioEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::AudioEventInfo::State::kUnderrun:
      return "Underrun ";
    case mojom::AudioEventInfo::State::kSevereUnderrun:
      return "Severe underrun";
  }
}

void OutputUsbEventInfo(const mojom::UsbEventInfoPtr& info) {
  base::Value::Dict output;

  output.Set("event", EnumToString(info->state));
  output.Set("vendor", info->vendor);
  output.Set("name", info->name);
  output.Set("vid", base::Value(info->vid));
  output.Set("pid", base::Value(info->pid));

  auto* categories = output.Set("categories", base::Value::List{});
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

void OutputLidEventInfo(const mojom::LidEventInfoPtr& info) {
  std::cout << "Lid event received: " << EnumToString(info->state) << std::endl;
}

void OutputAudioJackEventInfo(const mojom::AudioJackEventInfoPtr& info) {
  std::cout << "Audio jack event received: " << EnumToString(info->state)
            << std::endl;
}

void OutputSdCardEventInfo(const mojom::SdCardEventInfoPtr& info) {
  std::cout << "SdCard event received: " << EnumToString(info->state)
            << std::endl;
}

void OutputPowerEventInfo(const mojom::PowerEventInfoPtr& info) {
  std::cout << "Power event received: " << EnumToString(info->state)
            << std::endl;
}

void OutputAudioEventInfo(const mojom::AudioEventInfoPtr& info) {
  std::cout << "Audio event received: " << EnumToString(info->state)
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

void EventSubscriber::SubscribeToNetworkEvents() {
  mojo::PendingRemote<network_health_ipc::NetworkEventsObserver> remote;
  network_subscriber_ = std::make_unique<NetworkSubscriber>(
      remote.InitWithNewPipeAndPassReceiver());
  event_service_->AddNetworkObserver(std::move(remote));
}

void EventSubscriber::SubscribeToEvents(
    base::OnceClosure on_subscription_disconnect,
    mojom::EventCategoryEnum category) {
  event_service_->AddEventObserver(category,
                                   receiver_.BindNewPipeAndPassRemote());
  receiver_.set_disconnect_handler(std::move(on_subscription_disconnect));
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
      OutputLidEventInfo(info->get_lid_event_info());
      break;
    case mojom::EventInfo::Tag::kBluetoothEventInfo:
      NOTIMPLEMENTED();
      break;
    case mojom::EventInfo::Tag::kPowerEventInfo:
      OutputPowerEventInfo(info->get_power_event_info());
      break;
    case mojom::EventInfo::Tag::kAudioEventInfo:
      OutputAudioEventInfo(info->get_audio_event_info());
      break;
    case mojom::EventInfo::Tag::kAudioJackEventInfo:
      OutputAudioJackEventInfo(info->get_audio_jack_event_info());
      break;
    case mojom::EventInfo::Tag::kSdCardEventInfo:
      OutputSdCardEventInfo(info->get_sd_card_event_info());
      break;
  }
}

}  // namespace diagnostics
