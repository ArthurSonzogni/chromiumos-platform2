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
#include "biod/pairing_key_storage.h"
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
    std::unique_ptr<CrosFpSessionManager> session_manager,
    std::unique_ptr<PairingKeyStorage> pk_storage)
    : biod_metrics_(biod_metrics),
      cros_dev_(std::move(cros_fp_device)),
      power_button_filter_(std::move(power_button_filter)),
      session_manager_(std::move(session_manager)),
      pk_storage_(std::move(pk_storage)),
      session_weak_factory_(this) {
  CHECK(power_button_filter_);
  CHECK(cros_dev_);
  CHECK(biod_metrics_);
  CHECK(session_manager_);
  CHECK(pk_storage_);

  cros_dev_->SetMkbpEventCallback(base::BindRepeating(
      &CrosFpAuthStackManager::OnMkbpEvent, base::Unretained(this)));
}

bool CrosFpAuthStackManager::Initialize() {
  if (pk_storage_->PairingKeyExists()) {
    return LoadPairingKey();
  }
  // TODO(b/251738584): Establish Pk with GSC.
  return true;
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

  if (!session_manager_->GetUser().has_value()) {
    LOG(ERROR) << "Can only start enroll session when there is a user session.";
    return Session(base::NullCallback());
  }

  if (session_manager_->GetNumOfTemplates() >= cros_dev_->MaxTemplateCount()) {
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
  CreateCredentialReply reply;

  if (!CanCreateCredential()) {
    LOG(ERROR) << "Can't create credential now, current state is: "
               << CurrentStateToString();
    reply.set_status(CreateCredentialReply::INCORRECT_STATE);
    return reply;
  }

  std::optional<std::string> current_user_id = session_manager_->GetUser();
  if (!current_user_id.has_value() || request.user_id() != *current_user_id) {
    LOG(ERROR) << "Credential can only be created for the current user.";
    reply.set_status(CreateCredentialReply::INCORRECT_STATE);
    return reply;
  }

  if (!cros_dev_->SetNonceContext(
          brillo::BlobFromString(request.gsc_nonce()),
          brillo::BlobFromString(request.encrypted_label_seed()),
          brillo::BlobFromString(request.iv()))) {
    LOG(ERROR) << "Failed to set nonce context";
    reply.set_status(CreateCredentialReply::SET_NONCE_FAILED);
    return reply;
  }

  std::unique_ptr<VendorTemplate> tmpl =
      cros_dev_->GetTemplate(CrosFpDevice::kLastTemplate);
  if (!tmpl) {
    LOG(ERROR) << "Failed to retrieve enrolled finger";
    reply.set_status(CreateCredentialReply::NO_TEMPLATE);
    return reply;
  }

  std::optional<ec::CrosFpDeviceInterface::GetSecretReply> secret_reply =
      cros_dev_->GetPositiveMatchSecretWithPubkey(
          CrosFpDevice::kLastTemplate,
          brillo::BlobFromString(request.pub().x()),
          brillo::BlobFromString(request.pub().y()));
  if (!secret_reply.has_value()) {
    LOG(ERROR) << "Failed to get positive match secret.";
    reply.set_status(CreateCredentialReply::NO_SECRET);
    return reply;
  }

  std::string record_id = BiodStorage::GenerateNewRecordId();
  // Label and validation value are not used in the new AuthStack flow.
  BiodStorageInterface::RecordMetadata record{
      .record_format_version = kRecordFormatVersion,
      .record_id = record_id,
      .user_id = std::move(request.user_id()),
      .label = "",
      .validation_val = {},
  };
  VendorTemplate actual_tmpl = *tmpl;

  if (!session_manager_->CreateRecord(record, std::move(tmpl))) {
    LOG(ERROR) << "Failed to create record for template.";
    reply.set_status(CreateCredentialReply::CREATE_RECORD_FAILED);
    return reply;
  }

  // We need to upload the newly-enrolled template to the preloaded buffer, so
  // that we can load it properly with other preloaded templates the next time
  // we want to AuthenticateCredential.
  LOG(INFO) << "Upload record " << LogSafeID(record_id) << ".";
  if (!cros_dev_->PreloadTemplate(session_manager_->GetNumOfTemplates() - 1,
                                  std::move(actual_tmpl))) {
    LOG(ERROR) << "Preload template failed.";
    state_ = State::kLocked;
  } else {
    state_ = State::kNone;
  }

  reply.set_status(CreateCredentialReply::SUCCESS);
  reply.set_encrypted_secret(
      brillo::BlobToString(secret_reply->encrypted_secret));
  reply.set_iv(brillo::BlobToString(secret_reply->iv));
  reply.mutable_pub()->set_x(brillo::BlobToString(secret_reply->pk_out_x));
  reply.mutable_pub()->set_y(brillo::BlobToString(secret_reply->pk_out_y));
  reply.set_record_id(std::move(record_id));
  return reply;
}

BioSession CrosFpAuthStackManager::StartAuthSession(std::string user_id) {
  if (!CanStartAuth()) {
    LOG(ERROR) << "Can't start an auth session now, current state is: "
               << CurrentStateToString();
    return BioSession(base::NullCallback());
  }

  if (!LoadUser(user_id)) {
    LOG(ERROR) << "Failed to load user for authentication.";
    return BioSession(base::NullCallback());
  }

  // Make sure context is cleared before starting auth session.
  cros_dev_->ResetContext();
  if (!RequestMatchFingerDown())
    return BioSession(base::NullCallback());
  state_ = State::kAuth;

  return BioSession(base::BindOnce(&CrosFpAuthStackManager::EndAuthSession,
                                   session_weak_factory_.GetWeakPtr()));
}

AuthenticateCredentialReply CrosFpAuthStackManager::AuthenticateCredential(
    const AuthenticateCredentialRequest& request) {
  NOTREACHED();
  AuthenticateCredentialReply reply;
  return reply;
}

void CrosFpAuthStackManager::OnUserLoggedOut() {
  session_manager_->UnloadUser();
}

void CrosFpAuthStackManager::OnUserLoggedIn(const std::string& user_id) {
  LoadUser(user_id);
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

bool CrosFpAuthStackManager::LoadPairingKey() {
  std::optional<brillo::Blob> wrapped_pairing_key =
      pk_storage_->ReadWrappedPairingKey();
  if (!wrapped_pairing_key.has_value()) {
    LOG(ERROR) << "Failed to read Pk from storage.";
    return false;
  }

  if (!cros_dev_->LoadPairingKey(*wrapped_pairing_key)) {
    LOG(ERROR) << "Failed to load Pk.";
    return false;
  }
  return true;
}

void CrosFpAuthStackManager::OnEnrollScanDone(
    ScanResult result,
    const AuthStackManager::EnrollStatus& enroll_status,
    brillo::Blob auth_nonce) {
  on_enroll_scan_done_.Run(result, enroll_status, std::move(auth_nonce));
}

void CrosFpAuthStackManager::OnAuthScanDone(brillo::Blob auth_nonce) {
  on_auth_scan_done_.Run(std::move(auth_nonce));
}

void CrosFpAuthStackManager::OnSessionFailed() {
  on_session_failed_.Run();
}

bool CrosFpAuthStackManager::LoadUser(std::string user_id) {
  const std::optional<std::string>& current_user = session_manager_->GetUser();
  if (current_user.has_value() && current_user.value() == user_id) {
    // No action required, the user is already loaded.
    return true;
  } else if (current_user.has_value()) {
    session_manager_->UnloadUser();
  }
  if (!session_manager_->LoadUser(std::move(user_id))) {
    LOG(ERROR) << "Failed to start user session.";
    state_ = State::kLocked;
    return false;
  }
  std::vector<CrosFpSessionManager::SessionRecord> records =
      session_manager_->GetRecords();
  for (size_t i = 0; i < records.size(); i++) {
    const auto& record = records[i];
    // TODO(b/253993586): Send record format version metrics here.
    LOG(INFO) << "Upload record " << LogSafeID(record.record_metadata.record_id)
              << ".";
    if (!cros_dev_->PreloadTemplate(i, record.tmpl)) {
      LOG(ERROR) << "Preload template failed.";
      state_ = State::kLocked;
      return false;
    }
  }
  return true;
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

bool CrosFpAuthStackManager::RequestMatchFingerDown() {
  next_session_action_ = base::BindRepeating(
      &CrosFpAuthStackManager::OnMatchFingerDown, base::Unretained(this));
  if (!cros_dev_->SetFpMode(ec::FpMode(Mode::kFingerDown))) {
    next_session_action_ = SessionAction();
    LOG(ERROR) << "Failed to start finger down mode";
    return false;
  }
  return true;
}

void CrosFpAuthStackManager::OnMatchFingerDown(uint32_t event) {
  if (!(event & EC_MKBP_FP_FINGER_DOWN)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }

  std::optional<brillo::Blob> auth_nonce = cros_dev_->GetNonce();
  if (!auth_nonce) {
    LOG(ERROR) << "Failed to get auth nonce.";
    OnSessionFailed();
    return;
  }

  OnTaskComplete();
  state_ = State::kAuthDone;
  OnAuthScanDone(std::move(*auth_nonce));
}

std::string CrosFpAuthStackManager::CurrentStateToString() {
  switch (state_) {
    case State::kNone:
      return "None";
    case State::kEnroll:
      return "Enroll";
    case State::kEnrollDone:
      return "EnrollDone";
    case State::kAuth:
      return "Auth";
    case State::kAuthDone:
      return "AuthDone";
    case State::kLocked:
      return "Locked";
  }
}

bool CrosFpAuthStackManager::IsActiveState() {
  switch (state_) {
    case State::kEnroll:
    case State::kAuth:
      return true;
    case State::kNone:
    case State::kEnrollDone:
    case State::kAuthDone:
    case State::kLocked:
      return false;
  }
}

bool CrosFpAuthStackManager::CanStartEnroll() {
  switch (state_) {
    case State::kNone:
    case State::kEnrollDone:
      return true;
    case State::kEnroll:
    case State::kAuth:
    case State::kAuthDone:
    case State::kLocked:
      return false;
  }
}

bool CrosFpAuthStackManager::CanCreateCredential() {
  return state_ == State::kEnrollDone;
}

bool CrosFpAuthStackManager::CanStartAuth() {
  switch (state_) {
    case State::kNone:
    case State::kEnrollDone:
    case State::kAuthDone:
      return true;
    case State::kEnroll:
    case State::kAuth:
    case State::kLocked:
      return false;
  }
}

bool CrosFpAuthStackManager::CanAuthenticateCredential() {
  return state_ == State::kAuthDone;
}

}  // namespace biod
