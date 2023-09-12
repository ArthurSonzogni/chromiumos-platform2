// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/routine_v2_client.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/notreached.h>
#include <base/values.h>
#include <mojo/service_constants.h>

#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/cros_health_tool/output_util.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

base::Value::Dict ParseMemoryDetail(
    const mojom::MemoryRoutineDetailPtr& memory_detail) {
  base::Value::Dict output;
  base::Value::List passed_items;
  base::Value::List failed_items;

  for (auto passed_item : memory_detail->result->passed_items) {
    passed_items.Append(EnumToString(passed_item));
  }
  for (auto failed_item : memory_detail->result->failed_items) {
    failed_items.Append(EnumToString(failed_item));
  }

  SET_DICT(bytes_tested, memory_detail, &output);
  output.Set("passed_items", std::move(passed_items));
  output.Set("failed_items", std::move(failed_items));
  return output;
}

base::Value::Dict ParseAudioDriverDetail(
    const mojom::AudioDriverRoutineDetailPtr& audio_driver_detail) {
  base::Value::Dict output;

  SET_DICT(internal_card_detected, audio_driver_detail, &output);
  SET_DICT(audio_devices_succeed_to_open, audio_driver_detail, &output);

  return output;
}

base::Value::Dict ParseUfsLifetimeDetail(
    const mojom::UfsLifetimeRoutineDetailPtr& ufs_lifetime_detail) {
  base::Value::Dict output;

  SET_DICT(pre_eol_info, ufs_lifetime_detail, &output);
  SET_DICT(device_life_time_est_a, ufs_lifetime_detail, &output);
  SET_DICT(device_life_time_est_b, ufs_lifetime_detail, &output);

  return output;
}

void FormatJsonOutput(bool single_line_json, const base::Value::Dict& output) {
  if (single_line_json) {
    std::cout << "Output: ";
    OutputSingleLineJson(output);
    return;
  }
  std::cout << "Output: " << std::endl;
  OutputJson(output);
}

}  // namespace

RoutineV2Client::RoutineV2Client(
    mojo::Remote<mojom::CrosHealthdRoutinesService> routine_service,
    bool single_line_json)
    : routine_service_(std::move(routine_service)) {
  output_printer_ = base::BindRepeating(&FormatJsonOutput, single_line_json);
}

RoutineV2Client::~RoutineV2Client() = default;

void RoutineV2Client::OnRoutineStateChange(
    mojom::RoutineStatePtr state_update) {
  switch (state_update->state_union->which()) {
    case mojom::RoutineStateUnion::Tag::kUnrecognizedArgument: {
      NOTREACHED_NORETURN() << "Got unrecognized RoutineState";
      break;
    }
    case mojom::RoutineStateUnion::Tag::kFinished: {
      auto& finished_state = state_update->state_union->get_finished();
      std::cout << '\r' << "Running Progress: " << int(state_update->percentage)
                << std::endl;
      std::string passed_status =
          finished_state->has_passed ? "Passed" : "Failed";
      std::cout << ("Status: ") << passed_status << std::endl;
      const auto& detail = finished_state->detail;
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
          break;
        case mojom::RoutineDetail::Tag::kMemory:
          PrintOutput(ParseMemoryDetail(detail->get_memory()));
          break;
        case mojom::RoutineDetail::Tag::kAudioDriver:
          PrintOutput(ParseAudioDriverDetail(detail->get_audio_driver()));
          break;
        case mojom::RoutineDetail::Tag::kUfsLifetime:
          PrintOutput(ParseUfsLifetimeDetail(detail->get_ufs_lifetime()));
          break;
      }
      run_loop_.Quit();
      return;
    }
    case mojom::RoutineStateUnion::Tag::kInitialized: {
      std::cout << "Initialized" << std::endl;
      return;
    }
    case mojom::RoutineStateUnion::Tag::kWaiting: {
      std::cout << '\r' << "Waiting: "
                << state_update->state_union->get_waiting()->reason
                << std::endl;
      return;
    }
    case mojom::RoutineStateUnion::Tag::kRunning: {
      std::cout << '\r' << "Running Progress: " << int(state_update->percentage)
                << std::flush;
      return;
    }
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
  output_printer_.Run(output);
}

}  // namespace diagnostics
