// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/tpm_manager_client_impl.h"

#include <memory>
#include <utility>

#include <base/notreached.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "rmad/utils/dbus_utils.h"

namespace rmad {

namespace {

RoVerificationStatus TpmManagerRoStatusToRmadRoStatus(
    tpm_manager::RoVerificationStatus status) {
  switch (status) {
    case tpm_manager::RO_STATUS_NOT_TRIGGERED:
      return RoVerificationStatus::NOT_TRIGGERED;
    case tpm_manager::RO_STATUS_PASS:
      return RoVerificationStatus::PASS;
    case tpm_manager::RO_STATUS_FAIL:
      return RoVerificationStatus::FAIL;
    case tpm_manager::RO_STATUS_UNSUPPORTED:
      return RoVerificationStatus::UNSUPPORTED;
    default:
      break;
  }
  NOTREACHED();
  return RoVerificationStatus::UNSUPPORTED;
}

}  // namespace

TpmManagerClientImpl::TpmManagerClientImpl() {
  tpm_manager_proxy_ =
      std::make_unique<org::chromium::TpmManagerProxy>(GetSystemBus());
}

TpmManagerClientImpl::TpmManagerClientImpl(
    std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy)
    : tpm_manager_proxy_(std::move(tpm_manager_proxy)) {}

bool TpmManagerClientImpl::GetRoVerificationStatus(
    RoVerificationStatus* ro_verification_status) {
  tpm_manager::GetRoVerificationStatusRequest request;
  tpm_manager::GetRoVerificationStatusReply reply;

  brillo::ErrorPtr error;
  if (!tpm_manager_proxy_->GetRoVerificationStatus(request, &reply, &error) ||
      error) {
    LOG(ERROR)
        << "Failed to call GetRoVerificationStatus from tpm_manager proxy";
    return false;
  }

  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to get RO verification status. Error code "
               << reply.status();
    return false;
  }

  VLOG(1) << "Get RO verification status: " << reply.ro_verification_status();
  if (ro_verification_status) {
    *ro_verification_status =
        TpmManagerRoStatusToRmadRoStatus(reply.ro_verification_status());
  }
  return true;
}

}  // namespace rmad
