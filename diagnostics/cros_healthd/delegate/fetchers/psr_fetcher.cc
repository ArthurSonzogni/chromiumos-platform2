// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/psr_fetcher.h"

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/delegate/utils/psr_cmd.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

namespace internal {

mojom::PsrInfo::LogState ConvertLogStateToMojo(psr::LogState log_state) {
  switch (log_state) {
    case psr::LogState::kNotStarted:
      return mojom::PsrInfo::LogState::kNotStarted;
    case psr::LogState::kStarted:
      return mojom::PsrInfo::LogState::kStarted;
    case psr::LogState::kStopped:
      return mojom::PsrInfo::LogState::kStopped;
  }
}

mojom::PsrEvent::EventType ConvertPsrEventTypeToMojo(
    psr::EventType event_type) {
  switch (event_type) {
    case psr::EventType::kLogStart:
      return mojom::PsrEvent::EventType::kLogStart;
    case psr::EventType::kLogEnd:
      return mojom::PsrEvent::EventType::kLogEnd;
    case psr::EventType::kMissing:
      return mojom::PsrEvent::EventType::kMissing;
    case psr::EventType::kInvalid:
      return mojom::PsrEvent::EventType::kInvalid;
    case psr::EventType::kPrtcFailure:
      return mojom::PsrEvent::EventType::kPrtcFailure;
    case psr::EventType::kCsmeRecovery:
      return mojom::PsrEvent::EventType::kCsmeRecovery;
    case psr::EventType::kCsmeDamState:
      return mojom::PsrEvent::EventType::kCsmeDamState;
    case psr::EventType::kCsmeUnlockState:
      return mojom::PsrEvent::EventType::kCsmeUnlockState;
    case psr::EventType::kSvnIncrease:
      return mojom::PsrEvent::EventType::kSvnIncrease;
    case psr::EventType::kFwVersionChanged:
      return mojom::PsrEvent::EventType::kFwVersionChanged;
  }
}

}  // namespace internal

PsrFetcher::PsrFetcher() = default;

PsrFetcher::~PsrFetcher() = default;

mojom::GetPsrResultPtr PsrFetcher::FetchPsrInfo() {
  auto result = mojom::PsrInfo::New();

  // Treat a device that doesn't have /dev/mei0 as not supporting PSR.
  if (!base::PathExists(paths::dev::kMei0.ToFull())) {
    return mojom::GetPsrResult::NewInfo(std::move(result));
  }
  std::unique_ptr<psr::PsrCmdVirt> psr_cmd = CreatePsrCmd();
  if (!psr_cmd) {
    return mojom::GetPsrResult::NewError("Failed to create PsrCmd.");
  }

  if (std::optional<bool> check_psr_result =
          psr_cmd->CheckPlatformServiceRecord();
      !check_psr_result.has_value()) {
    return mojom::GetPsrResult::NewError("Check PSR is not working.");
  } else if (!check_psr_result.value()) {
    // PSR is not supported.
    return mojom::GetPsrResult::NewInfo(std::move(result));
  }

  psr::PsrHeciResp psr_res;
  result->is_supported = true;
  if (!psr_cmd->GetPlatformServiceRecord(psr_res)) {
    return mojom::GetPsrResult::NewError("Get PSR is not working.");
  }

  if (psr_res.psr_version.major != psr::kPsrVersionMajor ||
      psr_res.psr_version.minor != psr::kPsrVersionMinor) {
    return mojom::GetPsrResult::NewError("Requires PSR 2.0 version.");
  }

  result->log_state = internal::ConvertLogStateToMojo(psr_res.log_state);
  result->uuid =
      psr_cmd->IdToHexString(psr_res.psr_record.uuid, psr::kUuidLength);
  result->upid =
      psr_cmd->IdToHexString(psr_res.psr_record.upid, psr::kUpidLength);
  result->log_start_date = psr_res.psr_record.genesis_info.genesis_date;
  result->oem_name =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_info);
  result->oem_make =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_make_info);
  result->oem_model =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_model_info);
  result->manufacture_country = reinterpret_cast<char*>(
      psr_res.psr_record.genesis_info.manufacture_country);
  result->oem_data =
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_data);
  result->uptime_seconds =
      psr_res.psr_record.ledger_info
          .ledger_counter[psr::LedgerCounterIndex::kS0Seconds];
  result->s5_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS5];
  result->s4_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS4];
  result->s3_counter = psr_res.psr_record.ledger_info
                           .ledger_counter[psr::LedgerCounterIndex::kS0ToS3];
  result->warm_reset_counter =
      psr_res.psr_record.ledger_info
          .ledger_counter[psr::LedgerCounterIndex::kWarmReset];

  for (int i = 0; i < psr_res.psr_record.events_count; ++i) {
    auto event = psr_res.psr_record.events_info[i];
    auto tmp_event = mojom::PsrEvent::New();

    tmp_event->type = internal::ConvertPsrEventTypeToMojo(event.event_type);
    tmp_event->time = event.timestamp;
    tmp_event->data = event.data;
    result->events.push_back(std::move(tmp_event));
  }

  return mojom::GetPsrResult::NewInfo(std::move(result));
}

std::unique_ptr<psr::PsrCmdVirt> PsrFetcher::CreatePsrCmd() {
  return std::make_unique<psr::PsrCmd>(paths::dev::kMei0.ToFull());
}

}  // namespace diagnostics
