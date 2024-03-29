// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/network_bandwidth.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/expected.h>

#include "diagnostics/cros_healthd/executor/utils/scoped_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
NetworkBandwidthRoutine::Create(Context* context) {
  std::string oem_name;
  auto status =
      context->ground_truth()->PrepareRoutineNetworkBandwidth(oem_name);
  if (!status->is_supported()) {
    return base::unexpected(std::move(status));
  }
  return base::ok(
      base::WrapUnique(new NetworkBandwidthRoutine(context, oem_name)));
}

NetworkBandwidthRoutine::NetworkBandwidthRoutine(Context* context,
                                                 const std::string& oem_name)
    : context_(context), oem_name_(oem_name) {
  CHECK(context_);

  routine_output_ = mojom::NetworkBandwidthRoutineDetail::New();
}

NetworkBandwidthRoutine::~NetworkBandwidthRoutine() = default;

void NetworkBandwidthRoutine::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();
  RunNextStep();
}

void NetworkBandwidthRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);

  // TODO(b/320631510): Handle the disconnection of `receiver_`,
  // `scoped_process_control_download_` and `scoped_process_control_upload_`.
  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop("Unexpected flow in routine.");
      break;
    case TestStep::kDownload:
      SetupTimeoutCallback();
      context_->executor()->RunNetworkBandwidthTest(
          mojom::NetworkBandwidthTestType::kDownload, oem_name_,
          receiver_.BindNewPipeAndPassRemote(),
          scoped_process_control_download_.BindNewPipeAndPassReceiver(),
          base::BindOnce(&NetworkBandwidthRoutine::HandleBandwidthTestResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kUpload:
      SetupTimeoutCallback();
      context_->executor()->RunNetworkBandwidthTest(
          mojom::NetworkBandwidthTestType::kUpload, oem_name_,
          receiver_.BindNewPipeAndPassRemote(),
          scoped_process_control_upload_.BindNewPipeAndPassReceiver(),
          base::BindOnce(&NetworkBandwidthRoutine::HandleBandwidthTestResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*error=*/std::nullopt);
      break;
  }
}

void NetworkBandwidthRoutine::HandleBandwidthTestResponse(
    std::optional<double> average_speed) {
  timeout_callback_.Cancel();
  receiver_.reset();
  if (!average_speed.has_value()) {
    SetResultAndStop("Error running NDT");
    return;
  }

  switch (step_) {
    case TestStep::kInitialize:
    case TestStep::kComplete:
      SetResultAndStop("Unexpected flow in routine.");
      return;
    case TestStep::kDownload:
      scoped_process_control_download_.Reset();
      routine_output_->download_speed_kbps = average_speed.value();
      break;
    case TestStep::kUpload:
      scoped_process_control_upload_.Reset();
      routine_output_->upload_speed_kbps = average_speed.value();
      break;
  }
  RunNextStep();
}

void NetworkBandwidthRoutine::OnProgress(double speed_kbps, double percentage) {
  auto info = mojom::NetworkBandwidthRoutineRunningInfo::New();
  info->speed_kbps = speed_kbps;
  if (step_ == TestStep::kDownload) {
    info->type = mojom::NetworkBandwidthRoutineRunningInfo_Type::kDownload;
  } else if (step_ == TestStep::kUpload) {
    info->type = mojom::NetworkBandwidthRoutineRunningInfo_Type::kUpload;
  } else {
    SetResultAndStop("Unexpected flow in routine.");
    return;
  }
  SetRunningStateInfo(
      mojom::RoutineRunningInfo::NewNetworkBandwidth(std::move(info)));

  // Update the percentage.
  auto new_percentage =
      static_cast<uint8_t>(std::clamp(percentage, 0.0, 100.0) / 2);
  if (step_ == TestStep::kUpload) {
    new_percentage += 50;
  }
  if (new_percentage > state()->percentage && new_percentage < 100) {
    SetPercentage(new_percentage);
  }
}

void NetworkBandwidthRoutine::SetupTimeoutCallback() {
  timeout_callback_.Reset(
      base::BindOnce(&NetworkBandwidthRoutine::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, timeout_callback_.callback(), kRunningNdtTimeout);
}

void NetworkBandwidthRoutine::OnTimeoutOccurred() {
  if (step_ == TestStep::kDownload) {
    scoped_process_control_download_.Reset();
  } else if (step_ == TestStep::kUpload) {
    scoped_process_control_upload_.Reset();
  } else {
    SetResultAndStop("Unexpected flow in routine.");
    return;
  }
  SetResultAndStop("Routine timeout");
}

void NetworkBandwidthRoutine::SetResultAndStop(
    std::optional<std::string> error) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();

  if (error.has_value()) {
    RaiseException(error.value());
    return;
  }
  SetFinishedState(true, mojom::RoutineDetail::NewNetworkBandwidth(
                             std::move(routine_output_)));
}

}  // namespace diagnostics
