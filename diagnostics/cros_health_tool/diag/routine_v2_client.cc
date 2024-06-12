// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/routine_v2_client.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/notreached.h>
#include <base/values.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_health_tool/output_util.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"
#include "diagnostics/mojom/routine_output_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

base::Value::Dict ConvertToValue(const mojom::MemoryRoutineDetailPtr& detail) {
  base::Value::Dict output;
  base::Value::List passed_items;
  base::Value::List failed_items;

  for (auto passed_item : detail->result->passed_items) {
    passed_items.Append(EnumToString(passed_item));
  }
  for (auto failed_item : detail->result->failed_items) {
    failed_items.Append(EnumToString(failed_item));
  }

  SET_DICT(bytes_tested, detail, &output);
  output.Set("passed_items", std::move(passed_items));
  output.Set("failed_items", std::move(failed_items));
  return output;
}

base::Value::Dict ConvertToValue(
    const mojom::NetworkBandwidthRoutineRunningInfoPtr& running) {
  base::Value::Dict output;
  output.Set("type", EnumToString(running->type));
  output.Set("speed_kbps", std::move(running->speed_kbps));
  return output;
}

}  // namespace

RoutineV2Client::RoutineV2Client(
    mojo::Remote<mojom::CrosHealthdRoutinesService> routine_service,
    bool single_line_json)
    : routine_service_(std::move(routine_service)),
      single_line_json_(single_line_json) {}

RoutineV2Client::~RoutineV2Client() = default;

void RoutineV2Client::OnRoutineStateChange(mojom::RoutineStatePtr state) {
  switch (state->state_union->which()) {
    case mojom::RoutineStateUnion::Tag::kInitialized:
      OnInitializedState();
      return;
    case mojom::RoutineStateUnion::Tag::kRunning:
      OnRunningState(state->percentage, state->state_union->get_running());
      return;
    case mojom::RoutineStateUnion::Tag::kWaiting:
      OnWaitingState(state->state_union->get_waiting());
      return;
    case mojom::RoutineStateUnion::Tag::kFinished:
      OnFinishedState(state->percentage, state->state_union->get_finished());
      return;
    case mojom::RoutineStateUnion::Tag::kUnrecognizedArgument:
      NOTREACHED_NORETURN() << "Got unrecognized RoutineState";
  }
}

void RoutineV2Client::CreateRoutine(mojom::RoutineArgumentPtr argument) {
  routine_service_->CreateRoutine(std::move(argument),
                                  routine_control_.BindNewPipeAndPassReceiver(),
                                  receiver_.BindNewPipeAndPassRemote());
  routine_control_.set_disconnect_with_reason_handler(base::BindOnce(
      &RoutineV2Client::OnRoutineDisconnection, weak_factory_.GetWeakPtr()));
}

void RoutineV2Client::StartAndWaitUntilTerminated() {
  routine_control_->Start();
  run_loop_.Run();
}

void RoutineV2Client::OnRoutineDisconnection(uint32_t error,
                                             const std::string& message) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';

  std::cout << "Status: Error" << std::endl;
  base::Value::Dict output;
  SetJsonDictValue("error", error, &output);
  SetJsonDictValue("message", message, &output);
  PrintOutput(output);
  run_loop_.Quit();
}

void RoutineV2Client::PrintOutput(const base::Value::Dict& output) {
  if (single_line_json_) {
    std::cout << "Output: " << GetSingleLineJson(output) << std::endl;
    return;
  }
  std::cout << "Output: " << std::endl;
  OutputJson(output);
}

void RoutineV2Client::OnUnexpectedError(const std::string& message) {
  std::cout << "Status: Error" << std::endl;
  base::Value::Dict output;
  SetJsonDictValue("message", message, &output);
  PrintOutput(output);
  run_loop_.Quit();
}

void RoutineV2Client::OnInitializedState() {
  std::cout << "Initialized" << std::endl;
}

void RoutineV2Client::OnRunningState(
    uint8_t percentage, const mojom::RoutineStateRunningPtr& running) {
  if (!running->info) {
    std::cout << '\r' << "Running Progress: " << int(percentage) << std::flush;
    return;
  }

  base::Value::Dict running_value;
  switch (running->info->which()) {
    case mojom::RoutineRunningInfo::Tag::kUnrecognizedArgument: {
      NOTREACHED_NORETURN() << "Got unrecognized RoutineRunningInfo";
    }
    case mojom::RoutineRunningInfo::Tag::kNetworkBandwidth:
      running_value = ConvertToValue(running->info->get_network_bandwidth());
      break;
  }
  std::cout << '\r' << "Running Progress: " << int(percentage)
            << ", Info: " << GetSingleLineJson(running_value) << std::flush;
}

void RoutineV2Client::OnWaitingState(
    const mojom::RoutineStateWaitingPtr& waiting) {
  std::cout << '\r' << "Waiting: " << waiting->reason << "; "
            << waiting->message << std::endl;
  if (waiting->reason ==
      mojom::RoutineStateWaiting::Reason::kWaitingInteraction) {
    if (waiting->interaction.is_null()) {
      OnUnexpectedError("Waiting for null interaction");
      return;
    }

    switch (waiting->interaction->which()) {
      case mojom::RoutineInteraction::Tag::kUnrecognizedInteraction: {
        OnUnexpectedError("Unrecognized interaction");
        return;
      }
      case mojom::RoutineInteraction::Tag::kInquiry: {
        const auto& inquiry = waiting->interaction->get_inquiry();
        switch (inquiry->which()) {
          case mojom::RoutineInquiry::Tag::kUnrecognizedInquiry: {
            OnUnexpectedError("Unrecognized inquiry");
            return;
          }
          case mojom::RoutineInquiry::Tag::kCheckLedLitUpState: {
            HandleCheckLedLitUpStateInquiry(
                inquiry->get_check_led_lit_up_state());
            return;
          }
          case mojom::RoutineInquiry::Tag::kUnplugAcAdapterInquiry: {
            HandleUnplugAcAdapterInquiry(
                inquiry->get_unplug_ac_adapter_inquiry());
            return;
          }
        }
      }
    }
  }
}

void RoutineV2Client::OnFinishedState(
    uint8_t percentage, const mojom::RoutineStateFinishedPtr& finished) {
  std::cout << '\r' << "Running Progress: " << int(percentage) << std::endl;
  std::cout << "Status: " << (finished->has_passed ? "Passed" : "Failed")
            << std::endl;
  const auto& detail = finished->detail;
  if (!detail.is_null()) {
    switch (detail->which()) {
      case mojom::RoutineDetail::Tag::kUnrecognizedArgument: {
        NOTREACHED_NORETURN() << "Got unrecognized RoutineDetail";
        break;
      }
      case mojom::RoutineDetail::Tag::kMemory:
        PrintOutput(ConvertToValue(detail->get_memory()));
        break;
      case mojom::RoutineDetail::Tag::kAudioDriver:
        PrintOutput(ConvertToValue(detail->get_audio_driver()));
        break;
      case mojom::RoutineDetail::Tag::kUfsLifetime:
        PrintOutput(ConvertToValue(detail->get_ufs_lifetime()));
        break;
      case mojom::RoutineDetail::Tag::kBluetoothPower:
        PrintOutput(ConvertToValue(detail->get_bluetooth_power()));
        break;
      case mojom::RoutineDetail::Tag::kBluetoothDiscovery:
        PrintOutput(ConvertToValue(detail->get_bluetooth_discovery()));
        break;
      case mojom::RoutineDetail::Tag::kFan:
        PrintOutput(ConvertToValue(detail->get_fan()));
        break;
      case mojom::RoutineDetail::Tag::kBluetoothScanning:
        PrintOutput(ConvertToValue(detail->get_bluetooth_scanning()));
        break;
      case mojom::RoutineDetail::Tag::kBluetoothPairing:
        PrintOutput(ConvertToValue(detail->get_bluetooth_pairing()));
        break;
      case mojom::RoutineDetail::Tag::kCameraAvailability:
        PrintOutput(ConvertToValue(detail->get_camera_availability()));
        break;
      case mojom::RoutineDetail::Tag::kNetworkBandwidth:
        PrintOutput(ConvertToValue(detail->get_network_bandwidth()));
        break;
      case mojom::RoutineDetail::Tag::kSensitiveSensor:
        PrintOutput(ConvertToValue(detail->get_sensitive_sensor()));
        break;
      case mojom::RoutineDetail::Tag::kCameraFrameAnalysis:
        PrintOutput(ConvertToValue(detail->get_camera_frame_analysis()));
        break;
      case mojom::RoutineDetail::Tag::kBatteryDischarge:
        PrintOutput(ConvertToValue(detail->get_battery_discharge()));
        break;
    }
  }
  run_loop_.Quit();
}

void RoutineV2Client::HandleCheckLedLitUpStateInquiry(
    const mojom::CheckLedLitUpStateInquiryPtr& inquiry) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';

  std::optional<bool> answer;
  do {
    std::cout << "Is the LED lit up in the specified color? "
                 "Input y/n then press ENTER to continue."
              << std::endl;
    std::string input;
    std::getline(std::cin, input);

    if (!input.empty() && input[0] == 'y') {
      answer = true;
    } else if (!input.empty() && input[0] == 'n') {
      answer = false;
    }
  } while (!answer.has_value());

  CHECK(answer.has_value());
  routine_control_->ReplyInquiry(
      mojom::RoutineInquiryReply::NewCheckLedLitUpState(
          mojom::CheckLedLitUpStateReply::New(
              answer.value()
                  ? mojom::CheckLedLitUpStateReply::State::kCorrectColor
                  : mojom::CheckLedLitUpStateReply::State::kNotLitUp)));
}

void RoutineV2Client::HandleUnplugAcAdapterInquiry(
    const mojom::UnplugAcAdapterInquiryPtr& inquiry) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';
  std::cout << "Unplug the AC adapter.\n"
               "Press ENTER to continue."
            << std::endl;
  std::string input;
  std::getline(std::cin, input);
  routine_control_->ReplyInquiry(mojom::RoutineInquiryReply::NewUnplugAcAdapter(
      mojom::UnplugAcAdapterReply::New()));
}

}  // namespace diagnostics
