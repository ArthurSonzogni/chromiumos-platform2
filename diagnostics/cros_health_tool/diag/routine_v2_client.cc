// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/routine_v2_client.h"

#include <iostream>
#include <utility>

#include <base/values.h>
#include <mojo/service_constants.h>

#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/cros_health_tool/output_util.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

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
    : routine_service_(std::move(routine_service)),
      single_line_json_(single_line_json) {}

RoutineV2Client::~RoutineV2Client() = default;

void RoutineV2Client::CreateRoutine(mojom::RoutineArgumentPtr argument) {
  routine_service_->CreateRoutine(
      std::move(argument), routine_control_.BindNewPipeAndPassReceiver());
  routine_control_.set_disconnect_with_reason_handler(base::BindOnce(
      &RoutineV2Client::OnRoutineDisconnection, weak_factory_.GetWeakPtr()));
}

void RoutineV2Client::StartAndWaitUntilTerminated() {
  observer_.SetFormatOutputCallback(
      base::BindOnce(&FormatJsonOutput, single_line_json_));
  routine_control_->AddObserver(observer_.BindNewPipdAndPassRemote());
  routine_control_->Start();
  run_loop_.Run();
}

void RoutineV2Client::OnRoutineDisconnection(uint32_t error,
                                             const std::string& message) {
  std::cout << "Status: Error" << std::endl;
  base::Value::Dict output;
  SetJsonDictValue("error", error, &output);
  SetJsonDictValue("message", message, &output);
  FormatJsonOutput(single_line_json_, output);
  run_loop_.Quit();
}

}  // namespace diagnostics
