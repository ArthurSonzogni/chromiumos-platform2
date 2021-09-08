// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/check.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <dbus/object_proxy.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "diagnostics/common/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Tpm manager and attestation require a long timeout.
const int64_t DBUS_TIMEOUT_MS =
    base::TimeDelta::FromMinutes(2).InMilliseconds();

mojo_ipc::TpmGSCVersion GetGscVersion(
    const tpm_manager::GetVersionInfoReply& reply) {
  switch (reply.gsc_version()) {
    case tpm_manager::GSC_VERSION_NOT_GSC:
      return mojo_ipc::TpmGSCVersion::kNotGSC;
    case tpm_manager::GSC_VERSION_CR50:
      return mojo_ipc::TpmGSCVersion::kCr50;
    case tpm_manager::GSC_VERSION_TI50:
      return mojo_ipc::TpmGSCVersion::kTi50;
  }
}

}  // namespace

void TpmFetcher::FetchVersion() {
  tpm_manager::GetVersionInfoRequest request;
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&TpmFetcher::HandleVersion, weak_factory_.GetWeakPtr()));
  context_->tpm_manager_proxy()->GetVersionInfoAsync(
      request, std::move(on_success), std::move(on_error), DBUS_TIMEOUT_MS);
}

void TpmFetcher::HandleVersion(brillo::Error* err,
                               const tpm_manager::GetVersionInfoReply& reply) {
  DCHECK(info_);
  if (err) {
    SendError("Failed to call TpmManager::GetVersionInfo(): " +
              err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SendError("TpmManager::GetVersionInfo() returned error status: " +
              std::to_string(reply.status()));
    return;
  }
  auto version = mojo_ipc::TpmVersion::New();
  version->gsc_version = GetGscVersion(reply);
  version->family = reply.family();
  version->spec_level = reply.spec_level();
  version->manufacturer = reply.manufacturer();
  version->tpm_model = reply.tpm_model();
  version->firmware_version = reply.firmware_version();
  version->vendor_specific = reply.vendor_specific().empty()
                                 ? base::nullopt
                                 : base::make_optional(reply.vendor_specific());
  info_->version = std::move(version);
  CheckAndSendInfo();
}

void TpmFetcher::FetchStatus() {
  tpm_manager::GetTpmNonsensitiveStatusRequest request;
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&TpmFetcher::HandleStatus, weak_factory_.GetWeakPtr()));
  context_->tpm_manager_proxy()->GetTpmNonsensitiveStatusAsync(
      request, std::move(on_success), std::move(on_error), DBUS_TIMEOUT_MS);
}

void TpmFetcher::HandleStatus(
    brillo::Error* err,
    const tpm_manager::GetTpmNonsensitiveStatusReply& reply) {
  DCHECK(info_);
  if (err) {
    SendError("Failed to call TpmManager::GetTpmNonsensitiveStatus(): " +
              err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SendError("TpmManager::GetTpmNonsensitiveStatus() returned error status: " +
              std::to_string(reply.status()));
    return;
  }
  auto status = mojo_ipc::TpmStatus::New();
  status->enabled = reply.is_enabled();
  status->owned = reply.is_owned();
  status->owner_password_is_present = reply.is_owner_password_present();
  info_->status = std::move(status);
  CheckAndSendInfo();
}

void TpmFetcher::CheckAndSendInfo() {
  DCHECK(info_);
  if (!info_->version || !info_->status) {
    return;
  }
  SendResult(mojo_ipc::TpmResult::NewTpmInfo(std::move(info_)));
}

void TpmFetcher::SendError(const std::string& message) {
  SendResult(mojo_ipc::TpmResult::NewError(CreateAndLogProbeError(
      mojo_ipc::ErrorType::kServiceUnavailable, message)));
}

void TpmFetcher::SendResult(
    chromeos::cros_healthd::mojom::TpmResultPtr result) {
  // Invalid all weak ptrs to prevent other callbacks to be run.
  weak_factory_.InvalidateWeakPtrs();
  if (pending_callbacks_.empty())
    return;
  for (size_t i = 1; i < pending_callbacks_.size(); ++i) {
    std::move(pending_callbacks_[i]).Run(result.Clone());
  }
  std::move(pending_callbacks_[0]).Run(std::move(result));
  pending_callbacks_.clear();
}

void TpmFetcher::FetchTpmInfo(TpmFetcher::FetchTpmInfoCallback&& callback) {
  pending_callbacks_.push_back(std::move(callback));
  // Returns if there is already a pending callback. The second callback will be
  // fulfilled when the first one is fulfilled.
  if (pending_callbacks_.size() > 1)
    return;

  info_ = mojo_ipc::TpmInfo::New();
  FetchVersion();
  FetchStatus();
}

}  // namespace diagnostics
