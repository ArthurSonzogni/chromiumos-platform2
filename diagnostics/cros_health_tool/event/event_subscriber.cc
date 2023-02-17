// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
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

std::string EnumToString(mojom::BluetoothEventInfo::State state) {
  switch (state) {
    case mojom::BluetoothEventInfo::State::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::BluetoothEventInfo::State::kAdapterAdded:
      return "Adapter added";
    case mojom::BluetoothEventInfo::State::kAdapterRemoved:
      return "Adapter removed";
    case mojom::BluetoothEventInfo::State::kAdapterPropertyChanged:
      return "Adapter property changed";
    case mojom::BluetoothEventInfo::State::kDeviceAdded:
      return "Device added";
    case mojom::BluetoothEventInfo::State::kDeviceRemoved:
      return "Device removed";
    case mojom::BluetoothEventInfo::State::kDevicePropertyChanged:
      return "Device property changed";
  }
}

std::string EnumToString(mojom::InputTouchButton button) {
  switch (button) {
    case mojom::InputTouchButton::kUnmappedEnumField:
      LOG(FATAL) << "Got UnmappedEnumField";
      return "UnmappedEnumField";
    case mojom::InputTouchButton::kLeft:
      return "Left";
    case mojom::InputTouchButton::kMiddle:
      return "Middle";
    case mojom::InputTouchButton::kRight:
      return "Right";
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

void OutputBluetoothEventInfo(const mojom::BluetoothEventInfoPtr& info) {
  std::cout << "Bluetooth event received: " << EnumToString(info->state)
            << std::endl;
}

void OutputKeyboardDiagnosticEventInfo(
    const ash::diagnostics::mojom::KeyboardDiagnosticEventInfoPtr& info) {
  std::cout << "Keyboard diagnostic event received: the keybaord \""
            << info->keyboard_info->name << "\" got "
            << (info->tested_keys.size() + info->tested_top_row_keys.size())
            << " key(s) pressed." << std::endl;
}

void OutputTouchpadButtonEventInfo(
    const mojom::TouchpadButtonEventPtr& button_event) {
  base::Value::Dict output;
  output.Set("button", EnumToString(button_event->button));
  output.Set("pressed", button_event->pressed);

  std::string json;
  base::JSONWriter::Write(output, &json);
  std::cout << "Touchpad button event received: " << json << std::endl;
}

void OutputTouchpadTouchEventInfo(
    const mojom::TouchpadTouchEventPtr& touch_event) {
  base::Value::Dict output;
  base::Value::List touch_points;
  for (const auto& point : touch_event->touch_points) {
    base::Value::Dict point_dict;
    point_dict.Set("tracking_id", static_cast<double>(point->tracking_id));
    point_dict.Set("x", static_cast<double>(point->x));
    point_dict.Set("y", static_cast<double>(point->y));
    if (point->pressure) {
      point_dict.Set("pressure", static_cast<double>(point->pressure->value));
    }
    if (point->touch_major) {
      point_dict.Set("touch_major",
                     static_cast<double>(point->touch_major->value));
    }
    if (point->touch_minor) {
      point_dict.Set("touch_minor",
                     static_cast<double>(point->touch_minor->value));
    }
    touch_points.Append(std::move(point_dict));
  }
  output.Set("touch_points", std::move(touch_points));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION,
      &json);
  std::cout << "Touchpad touch event received: " << json << std::endl;
}

void OutputTouchpadConnectedEventInfo(
    const mojom::TouchpadConnectedEventPtr& connected_event) {
  base::Value::Dict output;
  output.Set("max_x", static_cast<double>(connected_event->max_x));
  output.Set("max_y", static_cast<double>(connected_event->max_y));
  output.Set("max_pressure",
             static_cast<double>(connected_event->max_pressure));

  auto* buttons = output.Set("buttons", base::Value::List{});
  for (const auto& button : connected_event->buttons) {
    buttons->Append(EnumToString(button));
  }

  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION,
      &json);
  std::cout << "Touchpad connected event received: " << json << std::endl;
}

void OutputTouchpadEventInfo(const mojom::TouchpadEventInfoPtr& info) {
  switch (info->which()) {
    case mojom::TouchpadEventInfo::Tag::kDefaultType:
      LOG(ERROR) << "Got TouchpadEventInfo::Tag::kDefaultType";
      break;
    case mojom::TouchpadEventInfo::Tag::kButtonEvent:
      OutputTouchpadButtonEventInfo(info->get_button_event());
      break;
    case mojom::TouchpadEventInfo::Tag::kTouchEvent:
      OutputTouchpadTouchEventInfo(info->get_touch_event());
      break;
    case mojom::TouchpadEventInfo::Tag::kConnectedEvent:
      OutputTouchpadConnectedEventInfo(info->get_connected_event());
      break;
  }
}

}  // namespace

EventSubscriber::EventSubscriber() {
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kCrosHealthdEvent, event_service_);
}

EventSubscriber::~EventSubscriber() = default;

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
  receiver_.set_disconnect_handler(
      base::BindOnce([]() {
        LOG(ERROR) << "The event observer has disconnected unexpectedly.";
      }).Then(std::move(on_subscription_disconnect)));
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
      OutputBluetoothEventInfo(info->get_bluetooth_event_info());
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
    case mojom::EventInfo::Tag::kKeyboardDiagnosticEventInfo:
      OutputKeyboardDiagnosticEventInfo(
          info->get_keyboard_diagnostic_event_info());
      break;
    case mojom::EventInfo::Tag::kTouchpadEventInfo:
      OutputTouchpadEventInfo(info->get_touchpad_event_info());
      break;
  }
}

}  // namespace diagnostics
