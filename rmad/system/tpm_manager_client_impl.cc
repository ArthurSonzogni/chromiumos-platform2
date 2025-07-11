// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/tpm_manager_client_impl.h"

#include <memory>
#include <utility>

#include <base/notreached.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "rmad/system/tpm_manager_client.h"
#include "rmad/utils/dbus_utils.h"

namespace rmad {

namespace {

RoVerificationStatus TpmManagerRoStatusToRmadRoStatus(
    tpm_manager::RoVerificationStatus status) {
  switch (status) {
    case tpm_manager::RO_STATUS_NOT_TRIGGERED:
      return RMAD_RO_VERIFICATION_NOT_TRIGGERED;
    case tpm_manager::RO_STATUS_PASS_UNVERIFIED_GBB:
      // Keep |RO_STATUS_PASS_UNVERIFIED_GBB| for backward compatibility with
      // the legacy Cr50 firmware.
      [[fallthrough]];
    case tpm_manager::RO_STATUS_PASS:
      return RMAD_RO_VERIFICATION_PASS;
    case tpm_manager::RO_STATUS_FAIL:
      return RMAD_RO_VERIFICATION_FAIL;
    case tpm_manager::RO_STATUS_UNSUPPORTED:
      // Deprecated.
      return RMAD_RO_VERIFICATION_UNSUPPORTED;
    case tpm_manager::RO_STATUS_UNSUPPORTED_NOT_TRIGGERED:
      return RMAD_RO_VERIFICATION_UNSUPPORTED_NOT_TRIGGERED;
    case tpm_manager::RO_STATUS_UNSUPPORTED_TRIGGERED:
      return RMAD_RO_VERIFICATION_UNSUPPORTED_TRIGGERED;
    default:
      break;
  }

  // We don't care about reported statuses from Ti50 (V2) unless they're mapped
  // to |RO_STATUS_PASS| with the RMA key combo.
  return RMAD_RO_VERIFICATION_UNSUPPORTED;
}

GscDevice TpmManagerGscDeviceToRmadGscDevice(tpm_manager::GscDevice device) {
  switch (device) {
    case tpm_manager::GSC_DEVICE_NOT_GSC:
      return GscDevice::GSC_DEVICE_NOT_GSC;
    case tpm_manager::GSC_DEVICE_H1:
      return GscDevice::GSC_DEVICE_H1;
    case tpm_manager::GSC_DEVICE_DT:
      return GscDevice::GSC_DEVICE_DT;
    case tpm_manager::GSC_DEVICE_NT:
      return GscDevice::GSC_DEVICE_NT;
    default:
      break;
  }
  NOTREACHED();
}

}  // namespace

TpmManagerClientImpl::TpmManagerClientImpl() {
  tpm_manager_proxy_ = std::make_unique<org::chromium::TpmManagerProxy>(
      DBus::GetInstance()->bus());
}

TpmManagerClientImpl::TpmManagerClientImpl(
    std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy)
    : tpm_manager_proxy_(std::move(tpm_manager_proxy)) {}

TpmManagerClientImpl::~TpmManagerClientImpl() = default;

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

bool TpmManagerClientImpl::GetGscDevice(GscDevice* gsc_device) {
  tpm_manager::GetVersionInfoRequest request;
  tpm_manager::GetVersionInfoReply reply;

  brillo::ErrorPtr error;
  if (!tpm_manager_proxy_->GetVersionInfo(request, &reply, &error) || error) {
    LOG(ERROR) << "Failed to call GetVersionInfo from tpm_manager proxy";
    return false;
  }

  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to get version info. Error code " << reply.status();
    return false;
  }

  if (gsc_device) {
    *gsc_device = TpmManagerGscDeviceToRmadGscDevice(reply.gsc_device());
  }
  return true;
}

}  // namespace rmad
