// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/routine_v2_client.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include <base/notreached.h>
#include <base/values.h>
#include <mojo/service_constants.h>

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
      OnRunningState(state->percentage);
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
    std::cout << "Output: ";
    OutputSingleLineJson(output);
    return;
  }
  std::cout << "Output: " << std::endl;
  OutputJson(output);
}

void RoutineV2Client::OnInitializedState() {
  std::cout << "Initialized" << std::endl;
}

void RoutineV2Client::OnRunningState(uint8_t percentage) {
  std::cout << '\r' << "Running Progress: " << int(percentage) << std::flush;
}

void RoutineV2Client::OnWaitingState(
    const mojom::RoutineStateWaitingPtr& waiting) {
  std::cout << '\r' << "Waiting: " << waiting->reason << std::endl;
}

void RoutineV2Client::OnFinishedState(
    uint8_t percentage, const mojom::RoutineStateFinishedPtr& finished) {
  std::cout << '\r' << "Running Progress: " << int(percentage) << std::endl;
  std::cout << "Status: " << (finished->has_passed ? "Passed" : "Failed")
            << std::endl;
  const auto& detail = finished->detail;
  switch (detail->which()) {
    case mojom::RoutineDetail::Tag::kUnrecognizedArgument: {
      NOTREACHED_NORETURN() << "Got unrecognized RoutineDetail";
      break;
    }
    // These routines do not produce printable output. Printing passed or
    // failed is enough.
    case mojom::RoutineDetail::Tag::kCpuStress:
    case mojom::RoutineDetail::Tag::kDiskRead:
    case mojom::RoutineDetail::Tag::kCpuCache:
    case mojom::RoutineDetail::Tag::kPrimeSearch:
    case mojom::RoutineDetail::Tag::kVolumeButton:
    case mojom::RoutineDetail::Tag::kLedLitUp:
    case mojom::RoutineDetail::Tag::kFloatingPoint:
    case mojom::RoutineDetail::Tag::kUrandom:
      break;
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
  }
  run_loop_.Quit();
}

}  // namespace diagnostics
