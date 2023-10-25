// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_discovery_v2.h"

#include <optional>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <dbus/object_path.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/floss_event_hub.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Time to wait for btmon to save HCI traces to log file.
constexpr base::TimeDelta kBluetoothDiscoveryRoutineBtmonWritingTime =
    base::Seconds(1);
// Maximum retry number of reading btmon logs to ensure btmon starts monitoring.
constexpr int kMaximumReadBtmonLogRetryNumber = 3;

// Target HCI commands.
constexpr char kHciCommandInquiry[] = "Inquiry";
constexpr char kHciCommandInquiryCancel[] = "Inquiry Cancel";
constexpr char kHciCommandLescan[] = "LE Set Scan Enable";
constexpr char kHciCommandExtendedLescan[] = "LE Set Extended Scan Enable";

// The result of parsing HCI traces from btmon logs. If the value is:
// * nullopt, the HCI command is not found.
// * false, the HCI command is found but the success event is not found.
// * true, the HCI command and success HCI event are both found.
struct BtmonDiscoveryResult {
  std::optional<bool> inquiry_on_result;
  std::optional<bool> inquiry_off_result;
  std::optional<bool> lescan_on_result;
  std::optional<bool> lescan_off_result;
};

void RemoveBtmonLog(mojom::Executor* executor) {
  executor->RemoveBtmonLog(base::DoNothing());
}

// Return true if |ReadBtmonLog| runs successfully.
bool HandleReadBtmonLogResponse(const mojom::ExecutedProcessResultPtr& result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;

  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "ReadBtmonLog failed with return code: " << return_code
               << " and error: " << err.c_str();
    return false;
  }
  return true;
}

// Check if the |Inquiry| and |LeScan Enabled| logs is found.
bool CheckDiscoveringOn(const BtmonDiscoveryResult& result) {
  return result.inquiry_on_result.value_or(false) &&
         result.lescan_on_result.value_or(false);
}

// Check if the |Inquiry Cancel| and |LeScan Disabled| logs is found.
bool CheckDiscoveringOff(const BtmonDiscoveryResult& result) {
  return result.inquiry_off_result.value_or(false) &&
         result.lescan_off_result.value_or(false);
}

}  // namespace

BluetoothDiscoveryRoutineV2::BluetoothDiscoveryRoutineV2(
    Context* context, const mojom::BluetoothDiscoveryRoutineArgumentPtr& arg)
    : BluetoothRoutineBaseV2(context) {
  CHECK(context_);

  routine_output_ = mojom::BluetoothDiscoveryRoutineDetail::New();
}

BluetoothDiscoveryRoutineV2::~BluetoothDiscoveryRoutineV2() = default;

void BluetoothDiscoveryRoutineV2::OnStart() {
  CHECK(step_ == TestStep::kInitialize);
  SetRunningState();

  start_ticks_ = base::TimeTicks::Now();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothDiscoveryRoutineV2::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      kDiscoveryRoutineTimeout);

  // Subscribe discovering changed.
  event_subscriptions_.push_back(
      context_->floss_event_hub()->SubscribeAdapterDiscoveringChanged(
          base::BindRepeating(
              &BluetoothDiscoveryRoutineV2::OnAdapterDiscoveringChanged,
              weak_ptr_factory_.GetWeakPtr())));

  Initialize(
      base::BindOnce(&BluetoothDiscoveryRoutineV2::HandleInitializeResult,
                     weak_ptr_factory_.GetWeakPtr()));
}

void BluetoothDiscoveryRoutineV2::HandleInitializeResult(bool success) {
  if (!success) {
    SetResultAndStop(
        base::unexpected("Failed to initialize Bluetooth routine."));
    return;
  }
  default_adapter_path_ =
      dbus::ObjectPath("/org/chromium/bluetooth/hci" +
                       base::NumberToString(default_adapter_hci_) + "/adapter");
  RunNextStep();
}

void BluetoothDiscoveryRoutineV2::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);
  UpdatePercentage();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
      break;
    case TestStep::kPreCheckDiscovery:
      RunPreCheck(
          base::BindOnce(&BluetoothDiscoveryRoutineV2::HandlePreCheckResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kEnsurePoweredOn: {
      if (GetAdapterInitialPoweredState()) {
        RunNextStep();
        return;
      }
      ChangeAdapterPoweredState(
          /*powered=*/true,
          base::BindOnce(
              &BluetoothDiscoveryRoutineV2::HandleEnsurePoweredOnResponse,
              weak_ptr_factory_.GetWeakPtr()));
      break;
    }
    case TestStep::kSetupBtmon: {
      // Set up scoped closure runner for removing btmon log file.
      remove_btmon_log_ = base::ScopedClosureRunner(
          base::BindOnce(&RemoveBtmonLog, context_->executor()));
      scoped_process_control_.AddOnTerminateCallback(
          base::ScopedClosureRunner(base::BindOnce(
              &BluetoothDiscoveryRoutineV2::SetResultAndStop,
              weak_ptr_factory_.GetWeakPtr(),
              base::unexpected("Btmon is terminated unexpectedly."))));
      // Although btmon will print the capturted HCI traces, it buffers the
      // stdout and we can't observe the expected events without new traces.
      // In that case, we will call |ReadBtmonLog| instead of reading the stdout
      // from |scoped_process_control_|.
      context_->executor()->StartBtmon(
          /*hci_interface=*/default_adapter_hci_,
          scoped_process_control_.BindNewPipeAndPassReceiver());
      // Check if btmon starts monitoring.
      ReadBtmonLog(/*retry_count=*/0);
      break;
    }
    case TestStep::kCheckDiscoveringStatusOn:
    case TestStep::kCheckDiscoveringStatusOff:
      UpdateAdapterDiscoveryMode();
      break;
    case TestStep::kComplete:
      SetResultAndStop(/*result=*/base::ok(true));
      break;
  }
}

void BluetoothDiscoveryRoutineV2::HandlePreCheckResponse(
    std::optional<std::string> error) {
  if (error.has_value()) {
    SetResultAndStop(base::unexpected(error.value()));
    return;
  }
  RunNextStep();
}

void BluetoothDiscoveryRoutineV2::HandleEnsurePoweredOnResponse(
    const base::expected<bool, std::string>& result) {
  if (!result.has_value() || !result.value()) {
    SetResultAndStop(
        base::unexpected("Failed to ensure default adapter is powered on."));
    return;
  }
  RunNextStep();
}

void BluetoothDiscoveryRoutineV2::UpdateAdapterDiscoveryMode() {
  auto adapter = GetDefaultAdapter();
  if (!adapter) {
    SetResultAndStop(base::unexpected("Failed to get default adapter."));
    return;
  }

  // Wait for the property changed event in |OnAdapterDiscoveringChanged|.
  if (step_ == TestStep::kCheckDiscoveringStatusOn) {
    SetupStopDiscoveryJob();
    adapter->StartDiscoveryAsync(
        base::DoNothing(),
        base::BindOnce(&BluetoothDiscoveryRoutineV2::HandleUpdateDiscoveryError,
                       weak_ptr_factory_.GetWeakPtr()));
  } else if (step_ == TestStep::kCheckDiscoveringStatusOff) {
    adapter_stop_discovery_.ReplaceClosure(base::DoNothing());
    adapter->CancelDiscoveryAsync(
        base::DoNothing(),
        base::BindOnce(&BluetoothDiscoveryRoutineV2::HandleUpdateDiscoveryError,
                       weak_ptr_factory_.GetWeakPtr()));
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }
}

void BluetoothDiscoveryRoutineV2::HandleUpdateDiscoveryError(
    brillo::Error* error) {
  SetResultAndStop(base::ok(false));
}

void BluetoothDiscoveryRoutineV2::ReadBtmonLog(int retry_count) {
  if (step_ == TestStep::kSetupBtmon) {
    context_->executor()->ReadBtmonLog(
        base::BindOnce(&BluetoothDiscoveryRoutineV2::EnsureBtmonReady,
                       weak_ptr_factory_.GetWeakPtr(), retry_count));
  } else if (step_ == TestStep::kCheckDiscoveringStatusOff ||
             step_ == TestStep::kCheckDiscoveringStatusOn) {
    context_->executor()->ReadBtmonLog(
        base::BindOnce(&BluetoothDiscoveryRoutineV2::CheckBtmonHciTraces,
                       weak_ptr_factory_.GetWeakPtr()));
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }
}

void BluetoothDiscoveryRoutineV2::EnsureBtmonReady(
    int retry_count,
    ash::cros_healthd::mojom::ExecutedProcessResultPtr result) {
  if (!HandleReadBtmonLogResponse(result)) {
    SetResultAndStop(base::unexpected("Failed to check btmon log file."));
    return;
  }

  const std::vector<std::string_view>& lines = base::SplitStringPiece(
      result->out, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  // Btmon will log more than one line if it is ready.
  if (lines.size() > 1) {
    RunNextStep();
    return;
  }

  if (retry_count >= kMaximumReadBtmonLogRetryNumber) {
    LOG(ERROR) << "Failed to ensure btmon is ready.";
    SetResultAndStop(base::unexpected("Failed to check btmon log file."));
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothDiscoveryRoutineV2::ReadBtmonLog,
                     weak_ptr_factory_.GetWeakPtr(), retry_count + 1),
      kBluetoothDiscoveryRoutineBtmonWritingTime);
}

void BluetoothDiscoveryRoutineV2::OnAdapterDiscoveringChanged(
    const dbus::ObjectPath& adapter_path, bool discovering) {
  if ((step_ != TestStep::kCheckDiscoveringStatusOn &&
       step_ != TestStep::kCheckDiscoveringStatusOff) ||
      adapter_path != default_adapter_path_)
    return;

  current_dbus_discovering_ = discovering;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&BluetoothDiscoveryRoutineV2::ReadBtmonLog,
                     weak_ptr_factory_.GetWeakPtr(), /*retry_num=*/0),
      kBluetoothDiscoveryRoutineBtmonWritingTime);
}

void BluetoothDiscoveryRoutineV2::CheckBtmonHciTraces(
    mojom::ExecutedProcessResultPtr result) {
  if (!HandleReadBtmonLogResponse(result)) {
    SetResultAndStop(base::unexpected("Failed to check btmon log file."));
    return;
  }

  // Regex for logs of HCI Command.
  const RE2 hci_cmd_regex(R"(^< HCI Command: ([A-Za-z ]*) \(.*\) plen)");
  const RE2 hci_lescan_param_regex(
      R"(^(?:Extended scan|Scanning): ([A-Za-z]*))");
  // Regex for logs of HCI Event.
  const RE2 event_cmd_complete_regex(
      R"(^> HCI Event: Command (Status|Complete) \(.*\) plen)");
  const RE2 hci_event_cmd_regex(
      R"((Inquiry|Inquiry Cancel|LE Set (Extended )?Scan Enable) \(.*\) ncmd)");

  const std::vector<std::string_view>& lines = base::SplitStringPiece(
      result->out, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  BtmonDiscoveryResult btmon_result;
  for (int i = log_line_last_checked; i < lines.size(); ++i) {
    std::string cmd_type;
    if (RE2::PartialMatch(lines[i], hci_cmd_regex, &cmd_type)) {
      LOG(INFO) << "Btmon - HCI Command: `" << cmd_type << "`";

      if (cmd_type == kHciCommandInquiry) {
        btmon_result.inquiry_on_result = false;
      } else if (cmd_type == kHciCommandInquiryCancel) {
        btmon_result.inquiry_off_result = false;
      } else if ((cmd_type == kHciCommandLescan ||
                  cmd_type == kHciCommandExtendedLescan) &&
                 i + 1 < lines.size()) {
        std::string lescan_param;
        if (!RE2::PartialMatch(lines[i + 1], hci_lescan_param_regex,
                               &lescan_param)) {
          continue;
        }
        if (lescan_param == "Enabled") {
          btmon_result.lescan_on_result = false;
        } else if (lescan_param == "Disabled") {
          btmon_result.lescan_off_result = false;
        }
      }
    } else if (RE2::PartialMatch(lines[i], event_cmd_complete_regex)) {
      if (i + 2 >= lines.size() ||
          !RE2::PartialMatch(lines[i + 1], hci_event_cmd_regex, &cmd_type)) {
        continue;
      }
      LOG(INFO) << "Btmon - HCI Event with command: `" << cmd_type << "`";

      bool is_success = lines[i + 2] == "Status: Success (0x00)";
      if (!is_success) {
        LOG(ERROR) << "Failed to get success event for: `" << cmd_type << "`";
        continue;
      }

      // Set the result to true if the HCI command is observed at first.
      if (cmd_type == kHciCommandInquiry) {
        if (btmon_result.inquiry_on_result.has_value()) {
          btmon_result.inquiry_on_result = true;
        }
      } else if (cmd_type == kHciCommandInquiryCancel) {
        if (btmon_result.inquiry_off_result.has_value()) {
          btmon_result.inquiry_off_result = true;
        }
      } else if (cmd_type == kHciCommandLescan ||
                 cmd_type == kHciCommandExtendedLescan) {
        if (btmon_result.lescan_on_result.has_value()) {
          btmon_result.lescan_on_result = true;
        } else if (btmon_result.lescan_off_result.has_value()) {
          btmon_result.lescan_off_result = true;
        }
      }
    }
  }
  // Update the line number to ignore previous logs.
  log_line_last_checked = lines.size();

  if (step_ == TestStep::kCheckDiscoveringStatusOn) {
    ValidateAdapterDiscovering(
        /*hci_discovering=*/CheckDiscoveringOn(btmon_result));
  } else if (step_ == TestStep::kCheckDiscoveringStatusOff) {
    ValidateAdapterDiscovering(
        /*hci_discovering=*/!CheckDiscoveringOff(btmon_result));
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }
}

void BluetoothDiscoveryRoutineV2::ValidateAdapterDiscovering(
    bool hci_discovering) {
  bool is_passed;
  auto discovering_state = mojom::BluetoothDiscoveringDetail::New();
  discovering_state->dbus_discovering = current_dbus_discovering_;
  discovering_state->hci_discovering = hci_discovering;

  if (step_ == TestStep::kCheckDiscoveringStatusOn) {
    // The discovering status should be true.
    is_passed = hci_discovering && current_dbus_discovering_;
    routine_output_->start_discovery_result = std::move(discovering_state);
  } else if (step_ == TestStep::kCheckDiscoveringStatusOff) {
    // The discovering status should be false.
    is_passed = !hci_discovering && !current_dbus_discovering_;
    routine_output_->stop_discovery_result = std::move(discovering_state);
  } else {
    SetResultAndStop(base::unexpected(kBluetoothRoutineUnexpectedFlow));
    return;
  }

  // Stop routine if validation is failed.
  if (!is_passed) {
    SetResultAndStop(/*result=*/base::ok(false));
    return;
  }
  RunNextStep();
}

void BluetoothDiscoveryRoutineV2::UpdatePercentage() {
  double new_percentage = static_cast<int32_t>(step_) * 100.0 /
                          static_cast<int32_t>(TestStep::kComplete);
  // Update the percentage.
  if (new_percentage > state()->percentage && new_percentage < 100)
    SetPercentage(new_percentage);
}

void BluetoothDiscoveryRoutineV2::OnTimeoutOccurred() {
  SetResultAndStop(
      base::unexpected("Bluetooth routine failed to complete before timeout."));
}

void BluetoothDiscoveryRoutineV2::SetResultAndStop(
    const base::expected<bool, std::string>& result) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  scoped_process_control_.Reset();
  adapter_stop_discovery_.RunAndReset();
  remove_btmon_log_.RunAndReset();
  reset_bluetooth_powered_.RunAndReset();

  if (!result.has_value()) {
    RaiseException(result.error());
    return;
  }
  SetFinishedState(result.value(), mojom::RoutineDetail::NewBluetoothDiscovery(
                                       std::move(routine_output_)));
}

}  // namespace diagnostics
