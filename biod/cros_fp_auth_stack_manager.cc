// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_auth_stack_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_record_manager.h"
#include "biod/power_button_filter_interface.h"
#include "biod/proto_bindings/constants.pb.h"
#include "biod/proto_bindings/messages.pb.h"
#include "biod/utils.h"

namespace biod {

// There is already a Session class in the biod namespace.
using BioSession = CrosFpAuthStackManager::Session;
using State = CrosFpAuthStackManager::State;
using Mode = ec::FpMode::Mode;

CrosFpAuthStackManager::CrosFpAuthStackManager(
    std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
    std::unique_ptr<ec::CrosFpDeviceInterface> cros_fp_device,
    BiodMetricsInterface* biod_metrics,
    std::unique_ptr<CrosFpRecordManagerInterface> record_manager)
    : biod_metrics_(biod_metrics),
      cros_dev_(std::move(cros_fp_device)),
      power_button_filter_(std::move(power_button_filter)),
      record_manager_(std::move(record_manager)),
      session_weak_factory_(this) {
  CHECK(power_button_filter_);
  CHECK(cros_dev_);
  CHECK(biod_metrics_);
  CHECK(record_manager_);

  cros_dev_->SetMkbpEventCallback(base::BindRepeating(
      &CrosFpAuthStackManager::OnMkbpEvent, base::Unretained(this)));
}

BiometricType CrosFpAuthStackManager::GetType() {
  return BIOMETRIC_TYPE_FINGERPRINT;
}

BioSession CrosFpAuthStackManager::StartEnrollSession() {
  if (!CanStartEnroll()) {
    LOG(ERROR) << "Can't start an enroll session now, current state is: "
               << CurrentStateToString();
    return BioSession(base::NullCallback());
  }

  if (loaded_records_.size() >= cros_dev_->MaxTemplateCount()) {
    LOG(ERROR) << "No space for an additional template.";
    return BioSession(base::NullCallback());
  }

  // Make sure context is cleared before starting enroll session.
  cros_dev_->ResetContext();
  if (!RequestEnrollImage())
    return BioSession(base::NullCallback());
  state_ = State::kEnroll;

  return BioSession(base::BindOnce(&CrosFpAuthStackManager::EndEnrollSession,
                                   session_weak_factory_.GetWeakPtr()));
}

CreateCredentialReply CrosFpAuthStackManager::CreateCredential(
    const CreateCredentialRequest& request) {
  NOTREACHED();
  CreateCredentialReply reply;
  return reply;
}

BioSession CrosFpAuthStackManager::StartAuthSession() {
  NOTREACHED();
  return BioSession(base::NullCallback());
}

AuthenticateCredentialReply CrosFpAuthStackManager::AuthenticateCredential(
    const AuthenticateCredentialRequest& request) {
  NOTREACHED();
  AuthenticateCredentialReply reply;
  return reply;
}

void CrosFpAuthStackManager::RemoveRecordsFromMemory() {
  NOTREACHED();
}

bool CrosFpAuthStackManager::ReadRecordsForSingleUser(
    const std::string& user_id) {
  NOTREACHED();
  return false;
}

void CrosFpAuthStackManager::SetEnrollScanDoneHandler(
    const AuthStackManager::EnrollScanDoneCallback& on_enroll_scan_done) {
  on_enroll_scan_done_ = on_enroll_scan_done;
}

void CrosFpAuthStackManager::SetAuthScanDoneHandler(
    const AuthStackManager::AuthScanDoneCallback& on_auth_scan_done) {
  on_auth_scan_done_ = on_auth_scan_done;
}

void CrosFpAuthStackManager::SetSessionFailedHandler(
    const AuthStackManager::SessionFailedCallback& on_session_failed) {
  on_session_failed_ = on_session_failed;
}

void CrosFpAuthStackManager::EndEnrollSession() {
  KillMcuSession();
}

void CrosFpAuthStackManager::EndAuthSession() {
  KillMcuSession();
}

void CrosFpAuthStackManager::KillMcuSession() {
  if (IsActiveState()) {
    state_ = State::kNone;
  }
  // TODO(b/274509408): test cros_dev_->FpMode(FP_MODE_DEEPSLEEP);
  cros_dev_->SetFpMode(ec::FpMode(Mode::kNone));
  session_weak_factory_.InvalidateWeakPtrs();
  OnTaskComplete();
}

void CrosFpAuthStackManager::OnMkbpEvent(uint32_t event) {
  if (!next_session_action_.is_null())
    next_session_action_.Run(event);
}

void CrosFpAuthStackManager::OnTaskComplete() {
  next_session_action_ = SessionAction();
}

void CrosFpAuthStackManager::OnEnrollScanDone(
    ScanResult result,
    const AuthStackManager::EnrollStatus& enroll_status,
    brillo::Blob auth_nonce) {
  on_enroll_scan_done_.Run(result, enroll_status, std::move(auth_nonce));
}

void CrosFpAuthStackManager::OnSessionFailed() {
  on_session_failed_.Run();
}

bool CrosFpAuthStackManager::RequestEnrollImage() {
  next_session_action_ = base::BindRepeating(
      &CrosFpAuthStackManager::DoEnrollImageEvent, base::Unretained(this));
  if (!cros_dev_->SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage))) {
    next_session_action_ = SessionAction();
    LOG(ERROR) << "Failed to start enrolling mode";
    return false;
  }
  return true;
}

bool CrosFpAuthStackManager::RequestEnrollFingerUp() {
  next_session_action_ = base::BindRepeating(
      &CrosFpAuthStackManager::DoEnrollFingerUpEvent, base::Unretained(this));
  if (!cros_dev_->SetFpMode(ec::FpMode(Mode::kEnrollSessionFingerUp))) {
    next_session_action_ = SessionAction();
    LOG(ERROR) << "Failed to wait for finger up";
    return false;
  }
  return true;
}

void CrosFpAuthStackManager::DoEnrollImageEvent(uint32_t event) {
  if (!(event & EC_MKBP_FP_ENROLL)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }

  int image_result = EC_MKBP_FP_ERRCODE(event);
  LOG(INFO) << __func__ << " result: '" << EnrollResultToString(image_result)
            << "'";
  ScanResult scan_result;
  switch (image_result) {
    case EC_MKBP_FP_ERR_ENROLL_OK:
      scan_result = ScanResult::SCAN_RESULT_SUCCESS;
      break;
    case EC_MKBP_FP_ERR_ENROLL_IMMOBILE:
      scan_result = ScanResult::SCAN_RESULT_IMMOBILE;
      break;
    case EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE:
      scan_result = ScanResult::SCAN_RESULT_PARTIAL;
      break;
    case EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY:
      scan_result = ScanResult::SCAN_RESULT_INSUFFICIENT;
      break;
    case EC_MKBP_FP_ERR_ENROLL_INTERNAL:
    default:
      LOG(ERROR) << "Unexpected result from capture: " << std::hex << event;
      OnSessionFailed();
      return;
  }

  int percent = EC_MKBP_FP_ENROLL_PROGRESS(event);

  if (percent < 100) {
    AuthStackManager::EnrollStatus enroll_status = {
        .done = false,
        .percent_complete = percent,
    };

    OnEnrollScanDone(scan_result, enroll_status, brillo::Blob());

    // The user needs to remove the finger before the next enrollment image.
    if (!RequestEnrollFingerUp())
      OnSessionFailed();

    return;
  }

  std::optional<brillo::Blob> auth_nonce = cros_dev_->GetNonce();
  if (!auth_nonce.has_value()) {
    LOG(ERROR) << "Failed to get auth nonce.";
    OnSessionFailed();
    return;
  }

  OnTaskComplete();
  state_ = State::kEnrollDone;
  AuthStackManager::EnrollStatus enroll_status = {
      .done = true,
      .percent_complete = 100,
  };
  OnEnrollScanDone(ScanResult::SCAN_RESULT_SUCCESS, enroll_status,
                   std::move(*auth_nonce));
}

void CrosFpAuthStackManager::DoEnrollFingerUpEvent(uint32_t event) {
  if (!(event & EC_MKBP_FP_FINGER_UP)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }

  if (!RequestEnrollImage())
    OnSessionFailed();
}

std::string CrosFpAuthStackManager::CurrentStateToString() {
  switch (state_) {
    case State::kNone:
      return "None";
    case State::kEnroll:
      return "Enroll";
    case State::kEnrollDone:
      return "EnrollDone";
  }
}

bool CrosFpAuthStackManager::IsActiveState() {
  switch (state_) {
    case State::kEnroll:
      return true;
    case State::kNone:
    case State::kEnrollDone:
      return false;
  }
}

bool CrosFpAuthStackManager::CanStartEnroll() {
  switch (state_) {
    case State::kNone:
    case State::kEnrollDone:
      return true;
    case State::kEnroll:
      return false;
  }
}

}  // namespace biod
