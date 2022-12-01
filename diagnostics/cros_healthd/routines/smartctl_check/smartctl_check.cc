// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check.h"

#include <memory>
#include <utility>

#include <base/base64.h>
#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/string_piece_forward.h>
#include <base/strings/string_split.h>
#include <debugd/dbus-proxies.h>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {

namespace {

// A scraper that is coupled to the format of smartctl -A.
// Sample output:
//   smartctl 7.1 2019-12-30 r5022 (...truncated)
//   Copyright (C) 2002-19, Bruce Allen, Christian Franke, www.smartmontools.org
//
//   === START OF SMART DATA SECTION ===
//   SMART/Health Information (NVMe Log 0x02)
//   Critical Warning:                   0x00
//   Temperature:                        47 Celsius
//   Available Spare:                    100%
//   Available Spare Threshold:          5%
//   Percentage Used:                    86%
//   Data Units Read:                    213,587,518 [109 TB]
//   Data Units Written:                 318,929,637 [163 TB]
//   (...truncated)
bool ScrapeSmartctlAttributes(const std::string& output,
                              int* available_spare,
                              int* available_spare_threshold) {
  bool found_available_spare = false;
  bool found_available_spare_threshold = false;
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(output, ':', '\n', &pairs);
  for (const auto& pair : pairs) {
    const std::string& key = pair.first;
    const base::StringPiece& value_str =
        base::TrimWhitespaceASCII(pair.second, base::TRIM_ALL);
    if (value_str.size() < 2) {
      continue;
    }

    int* target;
    bool* flag;
    if (key == "Available Spare") {
      target = available_spare;
      flag = &found_available_spare;
    } else if (key == "Available Spare Threshold") {
      target = available_spare_threshold;
      flag = &found_available_spare_threshold;
    } else {
      continue;
    }

    int value;
    if (base::StringToInt(value_str.substr(0, value_str.size() - 1), &value)) {
      *flag = true;
      if (target)
        *target = value;
    }

    if (found_available_spare && found_available_spare_threshold) {
      return true;
    }
  }
  return false;
}

}  // namespace

namespace mojom = ::ash::cros_healthd::mojom;

SmartctlCheckRoutine::SmartctlCheckRoutine(
    org::chromium::debugdProxyInterface* debugd_proxy)
    : debugd_proxy_(debugd_proxy) {
  DCHECK(debugd_proxy_);
}

SmartctlCheckRoutine::~SmartctlCheckRoutine() = default;

void SmartctlCheckRoutine::Start() {
  status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;

  auto result_callback =
      base::BindOnce(&SmartctlCheckRoutine::OnDebugdResultCallback,
                     weak_ptr_routine_.GetWeakPtr());
  auto error_callback =
      base::BindOnce(&SmartctlCheckRoutine::OnDebugdErrorCallback,
                     weak_ptr_routine_.GetWeakPtr());
  debugd_proxy_->SmartctlAsync("attributes", std::move(result_callback),
                               std::move(error_callback));
}

// The routine can only be started.
void SmartctlCheckRoutine::Resume() {}
void SmartctlCheckRoutine::Cancel() {}

void SmartctlCheckRoutine::UpdateStatus(
    mojom::DiagnosticRoutineStatusEnum status,
    uint32_t percent,
    std::string msg) {
  status_ = status;
  percent_ = percent;
  status_message_ = std::move(msg);
}

void SmartctlCheckRoutine::PopulateStatusUpdate(mojom::RoutineUpdate* response,
                                                bool include_output) {
  auto update = mojom::NonInteractiveRoutineUpdate::New();
  update->status = status_;
  update->status_message = status_message_;

  response->routine_update_union =
      mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
  response->progress_percent = percent_;

  if (include_output && !output_dict_.DictEmpty() &&
      (status_ == mojom::DiagnosticRoutineStatusEnum::kPassed ||
       status_ == mojom::DiagnosticRoutineStatusEnum::kFailed)) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        output_dict_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

mojom::DiagnosticRoutineStatusEnum SmartctlCheckRoutine::GetStatus() {
  return status_;
}

void SmartctlCheckRoutine::OnDebugdResultCallback(const std::string& result) {
  int available_spare;
  int available_spare_threshold;
  if (!ScrapeSmartctlAttributes(result, &available_spare,
                                &available_spare_threshold)) {
    LOG(ERROR) << "Unable to parse smartctl output: " << result;
    // TODO(b/260956052): Make the routine only available to NVMe, and return
    // kError in the parsing error.
    UpdateStatus(mojom::DiagnosticRoutineStatusEnum::kFailed,
                 /*percent=*/100, kSmartctlCheckRoutineFailedToParse);
    return;
  }

  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetIntKey("availableSpare", available_spare);
  result_dict.SetIntKey("availableSpareThreshold", available_spare_threshold);
  output_dict_.SetKey("resultDetails", std::move(result_dict));

  const bool available_spare_check_passed =
      available_spare >= available_spare_threshold;
  if (!available_spare_check_passed) {
    LOG(ERROR) << "available_spare (" << available_spare
               << "%) is less than available_spare_threshold ("
               << available_spare_threshold << "%)";
    UpdateStatus(mojom::DiagnosticRoutineStatusEnum::kFailed,
                 /*percent=*/100, kSmartctlCheckRoutineFailedAvailableSpare);
    return;
  }
  LOG(INFO) << "available_spare (" << available_spare
            << "%) is greater than available_spare_threshold ("
            << available_spare_threshold << "%)";
  UpdateStatus(mojom::DiagnosticRoutineStatusEnum::kPassed,
               /*percent=*/100, kSmartctlCheckRoutineSuccess);
}

void SmartctlCheckRoutine::OnDebugdErrorCallback(brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Debugd error: " << error->GetMessage();
    UpdateStatus(mojom::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kSmartctlCheckRoutineDebugdError);
  }
}

}  // namespace diagnostics
