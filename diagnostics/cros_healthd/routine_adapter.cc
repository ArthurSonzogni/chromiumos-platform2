// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routine_adapter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <base/check.h>
#include <base/notreached.h>
#include <mojo/public/cpp/bindings/remote_set.h>
#include <mojo/public/cpp/system/handle.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"
#include "diagnostics/mojom/routine_output_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::string EnumToString(mojom::MemtesterTestItemEnum subtest_enum) {
  switch (subtest_enum) {
    case mojom::MemtesterTestItemEnum::kStuckAddress:
      return "StuckAddress";
    case mojom::MemtesterTestItemEnum::kCompareAND:
      return "CompareAND";
    case mojom::MemtesterTestItemEnum::kCompareDIV:
      return "CompareDIV";
    case mojom::MemtesterTestItemEnum::kCompareMUL:
      return "CompareMUL";
    case mojom::MemtesterTestItemEnum::kCompareOR:
      return "CompareOR";
    case mojom::MemtesterTestItemEnum::kCompareSUB:
      return "CompareSUB";
    case mojom::MemtesterTestItemEnum::kCompareXOR:
      return "CompareXOR";
    case mojom::MemtesterTestItemEnum::kSequentialIncrement:
      return "SequentialIncrement";
    case mojom::MemtesterTestItemEnum::kBitFlip:
      return "BitFlip";
    case mojom::MemtesterTestItemEnum::kBitSpread:
      return "BitSpread";
    case mojom::MemtesterTestItemEnum::kBlockSequential:
      return "BlockSequential";
    case mojom::MemtesterTestItemEnum::kCheckerboard:
      return "Checkerboard";
    case mojom::MemtesterTestItemEnum::kRandomValue:
      return "RandomValue";
    case mojom::MemtesterTestItemEnum::kSolidBits:
      return "SolidBits";
    case mojom::MemtesterTestItemEnum::kWalkingOnes:
      return "WalkingOnes";
    case mojom::MemtesterTestItemEnum::kWalkingZeroes:
      return "WalkingZeroes";
    case mojom::MemtesterTestItemEnum::k8BitWrites:
      return "8-bitWrites";
    case mojom::MemtesterTestItemEnum::k16BitWrites:
      return "16-bitWrites";
    case mojom::MemtesterTestItemEnum::kUnmappedEnumField:
      LOG(ERROR) << "Unmapped subtest enum: " << subtest_enum;
      return "";
    case mojom::MemtesterTestItemEnum::kUnknown:
      LOG(ERROR) << "Unknown subtest enum: " << subtest_enum;
      return "";
  }
}

// Convert memory v2 routine detail to v1 format output.
base::Value::Dict ConvertMemoryV2ResultToOutputDict(
    const mojom::MemoryRoutineDetailPtr& memory_detail) {
  base::Value::Dict output_dict;
  // Holds the results of all subtests.
  base::Value::Dict subtest_dict;
  // Holds all the parsed output from memtester.
  base::Value::Dict result_dict;

  result_dict.Set("bytesTested",
                  base::NumberToString(memory_detail->bytes_tested));
  for (const auto& subtest_name : memory_detail->result->passed_items)
    subtest_dict.Set(EnumToString(subtest_name), "ok");
  for (const auto& subtest_name : memory_detail->result->failed_items)
    subtest_dict.Set(EnumToString(subtest_name), "failed");

  if (!subtest_dict.empty())
    result_dict.Set("subtests", std::move(subtest_dict));
  if (!result_dict.empty())
    output_dict.Set("resultDetails", std::move(result_dict));

  return output_dict;
}

base::Value::Dict ConvertRoutineDetailToOutputDict(
    const mojom::RoutineDetailPtr& detail) {
  switch (detail->which()) {
    case mojom::RoutineDetail::Tag::kUnrecognizedArgument: {
      NOTREACHED_NORETURN() << "Got unrecognized RoutineDetail";
    }
    // These routines do not produce printable output. Return empty output.
    case mojom::RoutineDetail::Tag::kCpuStress:
    case mojom::RoutineDetail::Tag::kDiskRead:
    case mojom::RoutineDetail::Tag::kCpuCache:
    case mojom::RoutineDetail::Tag::kPrimeSearch:
    case mojom::RoutineDetail::Tag::kVolumeButton:
    case mojom::RoutineDetail::Tag::kLedLitUp:
    case mojom::RoutineDetail::Tag::kFloatingPoint:
      return base::Value::Dict();
    case mojom::RoutineDetail::Tag::kMemory:
      return ConvertMemoryV2ResultToOutputDict(detail->get_memory());
    case mojom::RoutineDetail::Tag::kAudioDriver:
      return ParseAudioDriverDetail(detail->get_audio_driver());
    case mojom::RoutineDetail::Tag::kUfsLifetime:
      return ParseUfsLifetimeDetail(detail->get_ufs_lifetime());
    case mojom::RoutineDetail::Tag::kBluetoothPower:
      return ParseBluetoothPowerDetail(detail->get_bluetooth_power());
    case mojom::RoutineDetail::Tag::kBluetoothDiscovery:
      return ParseBluetoothDiscoveryDetail(detail->get_bluetooth_discovery());
    case mojom::RoutineDetail::Tag::kFan:
      return ParseFanDetail(detail->get_fan());
    case mojom::RoutineDetail::Tag::kBluetoothScanning:
      return ParseBluetoothScanningDetail(detail->get_bluetooth_scanning());
  }
}

mojo::ScopedHandle ConvertRoutineDetailToMojoHandle(
    const mojom::RoutineDetailPtr& detail) {
  std::string json;
  base::JSONWriter::Write(ConvertRoutineDetailToOutputDict(detail), &json);
  return CreateReadOnlySharedMemoryRegionMojoHandle(json);
}

}  // namespace

RoutineAdapter::RoutineAdapter(
    ash::cros_healthd::mojom::RoutineArgument::Tag routine_type)
    : routine_type_{routine_type} {
  error_occured_ = false;
  routine_cancelled_ = false;
  // We cannot guarantee when the observer will receive its first update,
  // therefore we cannot guarantee when the cached routine state will receive
  // its first update. Since in the old API a routine's availability check is
  // done before the routine is created, we assume that routine creation has
  // succeeded here and it is in running state.
  cached_state_ = mojom::RoutineState::New(
      0,
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New()));
}

RoutineAdapter::~RoutineAdapter() = default;

void RoutineAdapter::SetupAdapter(
    mojom::RoutineArgumentPtr arg,
    mojom::CrosHealthdRoutinesService* routine_service) {
  CHECK(routine_service);
  auto adapter = std::make_unique<RoutineAdapter>(arg->which());

  mojo::PendingReceiver<mojom::RoutineControl> pending_receiver =
      routine_control_.BindNewPipeAndPassReceiver();
  routine_control_.set_disconnect_with_reason_handler(base::BindRepeating(
      &RoutineAdapter::OnRoutineDisconnect, weak_ptr_factory_.GetWeakPtr()));

  routine_service->CreateRoutine(std::move(arg), std::move(pending_receiver),
                                 observer_receiver_.BindNewPipeAndPassRemote());
}

std::tuple<mojo::PendingReceiver<ash::cros_healthd::mojom::RoutineControl>,
           mojo::PendingRemote<ash::cros_healthd::mojom::RoutineObserver>>
RoutineAdapter::SetupRoutineControlAndObserver() {
  mojo::PendingReceiver<mojom::RoutineControl> pending_receiver =
      routine_control_.BindNewPipeAndPassReceiver();
  routine_control_.set_disconnect_with_reason_handler(base::BindOnce(
      &RoutineAdapter::OnRoutineDisconnect, weak_ptr_factory_.GetWeakPtr()));
  return std::make_tuple(std::move(pending_receiver),
                         observer_receiver_.BindNewPipeAndPassRemote());
}

void RoutineAdapter::OnRoutineStateChange(mojom::RoutineStatePtr state) {
  cached_state_ = std::move(state);
}

void RoutineAdapter::Start() {
  routine_control_->Start();
}

void RoutineAdapter::Resume() {
  NOTIMPLEMENTED();
}

void RoutineAdapter::Cancel() {
  routine_control_.reset();
  routine_cancelled_ = true;
  cached_state_ = mojom::RoutineState::New();
}

mojom::DiagnosticRoutineStatusEnum RoutineAdapter::GetStatus() {
  auto update = mojom::RoutineUpdate::New();
  PopulateStatusUpdate(update.get(), false);
  if (update->routine_update_union->is_noninteractive_update()) {
    return update->routine_update_union->get_noninteractive_update()->status;
  }
  // If the update is an interactive update, the status is kWaiting.
  return mojom::DiagnosticRoutineStatusEnum::kWaiting;
}

void RoutineAdapter::NotifyStatusChanged(
    ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status) {
  bool is_status_changed = last_status_ != status;
  last_status_ = status;

  if (is_status_changed) {
    for (const auto& callback : status_changed_callbacks_) {
      callback.Run(status);
    }
  }
}

void RoutineAdapter::RegisterStatusChangedCallback(
    StatusChangedCallback callback) {
  status_changed_callbacks_.push_back(std::move(callback));
}

void RoutineAdapter::PopulateStatusUpdate(
    ash::cros_healthd::mojom::RoutineUpdate* response, bool include_output) {
  CHECK(response);

  if (error_occured_) {
    auto update = mojom::NonInteractiveRoutineUpdate::New();
    update->status = mojom::DiagnosticRoutineStatusEnum::kError;
    update->status_message = error_message_;
    NotifyStatusChanged(update->status);
    response->routine_update_union =
        mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
    return;
  }

  if (routine_cancelled_) {
    auto update = mojom::NonInteractiveRoutineUpdate::New();
    update->status = mojom::DiagnosticRoutineStatusEnum::kCancelled;
    NotifyStatusChanged(update->status);
    response->routine_update_union =
        mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
    return;
  }

  if (routine_type_ == mojom::RoutineArgument::Tag::kUnrecognizedArgument) {
    auto update = mojom::NonInteractiveRoutineUpdate::New();
    update->status = mojom::DiagnosticRoutineStatusEnum::kUnknown;
    NotifyStatusChanged(update->status);
    response->routine_update_union =
        mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
    return;
  }

  CHECK(cached_state_);

  response->progress_percent = cached_state_->percentage;

  switch (cached_state_->state_union->which()) {
    case mojom::RoutineStateUnion::Tag::kUnrecognizedArgument: {
      NOTREACHED_NORETURN() << "Got unrecognized RoutineState";
      break;
    }
    case mojom::RoutineStateUnion::Tag::kInitialized: {
      auto update = mojom::NonInteractiveRoutineUpdate::New();
      update->status = mojom::DiagnosticRoutineStatusEnum::kRunning;
      NotifyStatusChanged(update->status);
      response->routine_update_union =
          mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
      return;
    }
    case mojom::RoutineStateUnion::Tag::kRunning: {
      auto update = mojom::NonInteractiveRoutineUpdate::New();
      update->status = mojom::DiagnosticRoutineStatusEnum::kRunning;
      NotifyStatusChanged(update->status);
      response->routine_update_union =
          mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
      return;
    }
    // For all status that is not kWaiting, the update is a non-interactive
    // update. We do not yet support routines that has a waiting state.
    case mojom::RoutineStateUnion::Tag::kWaiting: {
      const auto& waiting_ptr = cached_state_->state_union->get_waiting();
      auto update = mojom::NonInteractiveRoutineUpdate::New();
      switch (waiting_ptr->reason) {
        case mojom::RoutineStateWaiting::Reason::kWaitingToBeScheduled:
          // The behaviour of waiting in resource queue corresponds to kRunning
          // of the V1 API.
          update->status = mojom::DiagnosticRoutineStatusEnum::kRunning;
          break;
        case mojom::RoutineStateWaiting::Reason::kWaitingUserInput:
          update->status = mojom::DiagnosticRoutineStatusEnum::kWaiting;
          break;
        case mojom::RoutineStateWaiting::Reason::kUnmappedEnumField:
          update->status = mojom::DiagnosticRoutineStatusEnum::kWaiting;
          break;
      }
      update->status_message = waiting_ptr->message;
      NotifyStatusChanged(update->status);
      response->routine_update_union =
          mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
      return;
    }
    case mojom::RoutineStateUnion::Tag::kFinished: {
      const auto& finished_ptr = cached_state_->state_union->get_finished();
      auto update = mojom::NonInteractiveRoutineUpdate::New();
      update->status = finished_ptr->has_passed
                           ? mojom::DiagnosticRoutineStatusEnum::kPassed
                           : mojom::DiagnosticRoutineStatusEnum::kFailed;

      if (include_output) {
        if (finished_ptr->detail.is_null()) {
          update->status = mojom::DiagnosticRoutineStatusEnum::kError;
          update->status_message = "Got null routine output.";

        } else {
          response->output =
              ConvertRoutineDetailToMojoHandle(finished_ptr->detail);
        }
      }
      NotifyStatusChanged(update->status);
      response->routine_update_union =
          mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
      return;
    }
  }
}

void RoutineAdapter::OnRoutineDisconnect(uint32_t custom_reason,
                                         const std::string& message) {
  LOG(ERROR) << "Connection dropped by routine control.";
  error_occured_ = true;
  error_message_ = message;
  cached_state_ = mojom::RoutineState::New();
}

void RoutineAdapter::FlushRoutineControlForTesting() {
  routine_control_.FlushForTesting();
}

mojo::Remote<ash::cros_healthd::mojom::RoutineControl>&
RoutineAdapter::routine_control() {
  return routine_control_;
}

}  // namespace diagnostics
