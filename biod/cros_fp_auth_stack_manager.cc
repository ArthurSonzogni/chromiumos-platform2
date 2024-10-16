// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_auth_stack_manager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>
#include <libhwsec/status.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_record_manager.h"
#include "biod/maintenance_scheduler.h"
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
using PinWeaverEccPoint = hwsec::PinWeaverManagerFrontend::PinWeaverEccPoint;
using hwsec::PinWeaverEccPointSize;
using hwsec::PinWeaverManagerFrontend::AuthChannel::kFingerprintAuthChannel;
using StartEnrollSessionStatus = BiodMetricsInterface::StartEnrollSessionStatus;
using StartAuthSessionStatus = BiodMetricsInterface::StartAuthSessionStatus;
using EnrollLegacyTemplateStatus =
    BiodMetricsInterface::EnrollLegacyTemplateStatus;

CrosFpAuthStackManager::CrosFpAuthStackManager(
    std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
    std::unique_ptr<ec::CrosFpDeviceInterface> cros_fp_device,
    BiodMetricsInterface* biod_metrics,
    std::unique_ptr<CrosFpSessionManager> session_manager,
    std::unique_ptr<PairingKeyStorage> pk_storage,
    std::unique_ptr<const hwsec::PinWeaverManagerFrontend> pinweaver_manager,
    std::unique_ptr<CrosFpSessionManager> legacy_session_manager,
    State state,
    std::optional<uint32_t> pending_match_event)
    : biod_metrics_(biod_metrics),
      cros_dev_(std::move(cros_fp_device)),
      power_button_filter_(std::move(power_button_filter)),
      session_manager_(std::move(session_manager)),
      pk_storage_(std::move(pk_storage)),
      pinweaver_manager_(std::move(pinweaver_manager)),
      state_(state),
      pending_match_event_(pending_match_event),
      maintenance_scheduler_(std::make_unique<MaintenanceScheduler>(
          cros_dev_.get(), biod_metrics_)),
      migrator_(std::make_unique<CrosFpMigrator>(
          this, std::move(legacy_session_manager))),
      session_weak_factory_(this) {
  CHECK(power_button_filter_);
  CHECK(cros_dev_);
  CHECK(biod_metrics_);
  CHECK(session_manager_);
  CHECK(pk_storage_);
  CHECK(pinweaver_manager_);
  CHECK(migrator_);

  cros_dev_->SetMkbpEventCallback(base::BindRepeating(
      &CrosFpAuthStackManager::OnMkbpEvent, base::Unretained(this)));

  maintenance_scheduler_->Start();
}

bool CrosFpAuthStackManager::Initialize() {
  if (!pk_storage_->PairingKeyExists() && !EstablishPairingKey()) {
    return false;
  }
  return LoadPairingKey();
}

bool CrosFpAuthStackManager::EstablishPairingKey() {
  if (!pinweaver_manager_->IsEnabled().value_or(false)) {
    LOG(ERROR) << __func__ << "PinWeaver is not enabled.";
    return false;
  }

  // Pk related mechanisms are only added in PW version 2.
  if (pinweaver_manager_->GetVersion().value_or(0) <= 1) {
    LOG(ERROR) << __func__ << "PinWeaver version isn't new enough.";
    return false;
  }

  // Step 1: Keygen in FPMCU.
  std::optional<ec::CrosFpDeviceInterface::PairingKeyKeygenReply> reply =
      cros_dev_->PairingKeyKeygen();
  if (!reply.has_value()) {
    return false;
  }
  if (reply->pub_x.size() != PinWeaverEccPointSize ||
      reply->pub_y.size() != PinWeaverEccPointSize) {
    LOG(ERROR) << __func__
               << "Point size in PairingKeyKeygenReply is incorrect.";
    return false;
  }

  // Step 2: Keygen in GSC.
  PinWeaverEccPoint pub_in;
  std::copy(reply->pub_x.begin(), reply->pub_x.end(), pub_in.x);
  std::copy(reply->pub_y.begin(), reply->pub_y.end(), pub_in.y);
  hwsec::StatusOr<PinWeaverEccPoint> pub_out =
      pinweaver_manager_->GeneratePk(kFingerprintAuthChannel, pub_in);
  if (!pub_out.ok()) {
    LOG(ERROR) << __func__ << "GeneratePk for GSC failed.";
    return false;
  }

  // Step 3: Finish Pk establishment and retrieve it from FPMCU.
  std::optional<brillo::Blob> encrypted_pairing_key = cros_dev_->PairingKeyWrap(
      brillo::Blob(pub_out->x, pub_out->x + PinWeaverEccPointSize),
      brillo::Blob(pub_out->y, pub_out->y + PinWeaverEccPointSize),
      reply->encrypted_private_key);
  if (!encrypted_pairing_key.has_value()) {
    return false;
  }
  if (!pk_storage_->WriteWrappedPairingKey(*encrypted_pairing_key)) {
    LOG(ERROR) << "Failed to persist Pk.";
    return false;
  }

  return true;
}

BiometricType CrosFpAuthStackManager::GetType() {
  return BIOMETRIC_TYPE_FINGERPRINT;
}

GetNonceReply CrosFpAuthStackManager::GetNonce() {
  GetNonceReply reply;
  std::optional<brillo::Blob> nonce = cros_dev_->GetNonce();
  if (!nonce.has_value()) {
    LOG(ERROR) << "Failed to get nonce.";
    return reply;
  }
  reply.set_nonce(brillo::BlobToString(*nonce));
  return reply;
}

BioSession CrosFpAuthStackManager::StartEnrollSession(
    const StartEnrollSessionRequest& request) {
  if (!CanStartEnroll()) {
    LOG(ERROR) << "Can't start an enroll session now, current state is: "
               << CurrentStateToString();
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kIncorrectState);
    return BioSession(base::NullCallback());
  }

  const std::optional<std::string>& user_id = session_manager_->GetUser();
  if (!user_id.has_value()) {
    LOG(ERROR) << "Can only start enroll session when there is a user session.";
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kIncorrectState);
    return Session(base::NullCallback());
  }

  size_t num_of_templates = session_manager_->GetNumOfTemplates();
  if (num_of_templates >= cros_dev_->MaxTemplateCount()) {
    LOG(ERROR) << "No space for an additional template.";
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kTemplatesFull);
    return BioSession(base::NullCallback());
  }

  if (!cros_dev_->SetNonceContext(
          brillo::BlobFromString(request.gsc_nonce()),
          brillo::BlobFromString(request.encrypted_label_seed()),
          brillo::BlobFromString(request.iv()))) {
    LOG(ERROR) << "Failed to set nonce context";
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kSetContextFailed);
    return BioSession(base::NullCallback());
  }

  if (!cros_dev_->UnlockTemplates(num_of_templates)) {
    LOG(ERROR) << "Failed to unlock templates.";
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kUnlockTemplatesFailed);
    return BioSession(base::NullCallback());
  }

  if (!RequestEnrollImage()) {
    LOG(ERROR) << "Failed to set enroll image mode.";
    biod_metrics_->SendStartEnrollSessionStatus(
        StartEnrollSessionStatus::kSetEnrollModeFailed);
    return BioSession(base::NullCallback());
  }
  state_ = State::kEnroll;
  num_enrollment_captures_ = 0;

  biod_metrics_->SendStartEnrollSessionStatus(
      StartEnrollSessionStatus::kSuccess);
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

  const std::optional<std::string>& user_id = session_manager_->GetUser();
  if (!user_id.has_value()) {
    LOG(ERROR) << "Can only create credential when there is a user session.";
    reply.set_status(CreateCredentialReply::INCORRECT_STATE);
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
      .user_id = *user_id,
      .label = "",
      .validation_val = {},
  };
  VendorTemplate actual_tmpl = *tmpl;

  if (!session_manager_->CreateRecord(record, std::move(tmpl))) {
    LOG(ERROR) << "Failed to create record for template.";
    reply.set_status(CreateCredentialReply::CREATE_RECORD_FAILED);
    return reply;
  }

  state_ = State::kNone;
  reply.set_status(CreateCredentialReply::SUCCESS);
  reply.set_encrypted_secret(
      brillo::BlobToString(secret_reply->encrypted_secret));
  reply.set_iv(brillo::BlobToString(secret_reply->iv));
  reply.mutable_pub()->set_x(brillo::BlobToString(secret_reply->pk_out_x));
  reply.mutable_pub()->set_y(brillo::BlobToString(secret_reply->pk_out_y));
  reply.set_record_id(std::move(record_id));
  return reply;
}

BioSession CrosFpAuthStackManager::StartAuthSession(
    const StartAuthSessionRequest& request) {
  if (!CanStartAuth()) {
    LOG(ERROR) << "Can't start an auth session now, current state is: "
               << CurrentStateToString();
    biod_metrics_->SendStartAuthSessionStatus(
        StartAuthSessionStatus::kIncorrectState);
    return BioSession(base::NullCallback());
  }

  if (!LoadUser(request.user_id(), false)) {
    LOG(ERROR) << "Failed to load user for authentication.";
    biod_metrics_->SendStartAuthSessionStatus(
        StartAuthSessionStatus::kLoadUserFailed);
    return BioSession(base::NullCallback());
  }

  if (state_ == State::kWaitForFingerUp) {
    LOG(INFO) << "Received StartAuthSession while finger is down. Wait for "
                 "finger up before proceeding.";
    state_ = State::kAuthWaitForFingerUp;
    pending_request_ = request;
    biod_metrics_->SendStartAuthSessionStatus(
        StartAuthSessionStatus::kPendingFingerUp);
  } else {
    StartAuthSessionStatus status = PrepareStartAuthSession(request);
    biod_metrics_->SendStartAuthSessionStatus(status);
    if (status != StartAuthSessionStatus::kSuccess) {
      LOG(ERROR) << "Failed to prepare start auth session";
      return BioSession(base::NullCallback());
    }
    state_ = State::kAuth;
  }

  return BioSession(base::BindOnce(&CrosFpAuthStackManager::EndAuthSession,
                                   session_weak_factory_.GetWeakPtr()));
}

void CrosFpAuthStackManager::AuthenticateCredential(
    const AuthenticateCredentialRequest& request,
    AuthStackManager::AuthenticateCredentialCallback callback) {
  AuthenticateCredentialReply reply;

  if (!CanAuthenticateCredential()) {
    LOG(ERROR) << "Can't authenticate credential now, current state is: "
               << CurrentStateToString();
    reply.set_status(AuthenticateCredentialReply::INCORRECT_STATE);
    std::move(callback).Run(std::move(reply));
    return;
  }

  std::optional<uint32_t> event;
  event.swap(pending_match_event_);
  if (!event.has_value()) {
    LOG(ERROR) << "No match event.";
    reply.set_status(AuthenticateCredentialReply::INCORRECT_STATE);
    std::move(callback).Run(std::move(reply));
    return;
  }

  // Don't try to match again until the user has lifted their finger from the
  // sensor. Request the FingerUp event as soon as the HW signaled a match so it
  // doesn't attempt a new match while the host is processing the first
  // match event.
  if (!RequestFingerUp()) {
    state_ = State::kNone;
    LOG(WARNING) << "Failed to request finger up.";
  } else {
    state_ = State::kWaitForFingerUp;
  }

  if (power_button_filter_->ShouldFilterFingerprintMatch()) {
    LOG(INFO) << "Power button event seen along with fp match. Treating it as "
                 "an error.";
    biod_metrics_->SendIgnoreMatchEventOnPowerButtonPress(true);

    reply.set_status(AuthenticateCredentialReply::SUCCESS);
    reply.set_scan_result(ScanResult::SCAN_RESULT_POWER_BUTTON_PRESSED);
    std::move(callback).Run(std::move(reply));
    return;
  }
  biod_metrics_->SendIgnoreMatchEventOnPowerButtonPress(false);

  int match_result = EC_MKBP_FP_ERRCODE(*event);
  bool matched = false;

  uint32_t match_idx = EC_MKBP_FP_MATCH_IDX(*event);
  LOG(INFO) << __func__ << " result: '" << MatchResultToString(match_result)
            << "' (finger: " << match_idx << ")";

  switch (match_result) {
    case EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES:
      LOG(ERROR) << "No templates to match: " << std::hex << *event;
      reply.set_status(AuthenticateCredentialReply::NO_TEMPLATES);
      break;
    case EC_MKBP_FP_ERR_MATCH_NO_INTERNAL:
      LOG(ERROR) << "Internal error when matching templates: " << std::hex
                 << *event;
      reply.set_status(AuthenticateCredentialReply::INTERNAL_ERROR);
      break;
    case EC_MKBP_FP_ERR_MATCH_NO:
      reply.set_status(AuthenticateCredentialReply::SUCCESS);
      reply.set_scan_result(ScanResult::SCAN_RESULT_NO_MATCH);
      break;
    case EC_MKBP_FP_ERR_MATCH_YES:
    case EC_MKBP_FP_ERR_MATCH_YES_UPDATED:
    case EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED:
      // We are on a good path to successfully authenticate user, but
      // we still need to fetch the positive match secret.
      matched = true;
      break;
    case EC_MKBP_FP_ERR_MATCH_NO_LOW_QUALITY:
      reply.set_status(AuthenticateCredentialReply::SUCCESS);
      reply.set_scan_result(ScanResult::SCAN_RESULT_INSUFFICIENT);
      break;
    case EC_MKBP_FP_ERR_MATCH_NO_LOW_COVERAGE:
      reply.set_status(AuthenticateCredentialReply::SUCCESS);
      reply.set_scan_result(ScanResult::SCAN_RESULT_PARTIAL);
      break;
    default:
      LOG(ERROR) << "Unexpected result from matching templates: " << std::hex
                 << *event;
      reply.set_status(AuthenticateCredentialReply::INTERNAL_ERROR);
  }

  // Report match latency after responding to the callback to avoid blocking the
  // operation.
  base::ScopedClosureRunner report_match_latency(
      base::BindOnce(&CrosFpAuthStackManager::ReportMatchLatency,
                     session_weak_factory_.GetWeakPtr(), matched));

  if (!matched) {
    std::move(callback).Run(std::move(reply));
    return;
  }

  std::optional<RecordMetadata> metadata =
      session_manager_->GetRecordMetadata(match_idx);
  if (!metadata.has_value()) {
    LOG(ERROR) << "Matched template idx not found in in-memory records.";
    reply.set_status(AuthenticateCredentialReply::NO_TEMPLATES);
    std::move(callback).Run(std::move(reply));
    return;
  }

  std::optional<ec::CrosFpDeviceInterface::GetSecretReply> secret_reply =
      cros_dev_->GetPositiveMatchSecretWithPubkey(
          match_idx, brillo::BlobFromString(request.pub().x()),
          brillo::BlobFromString(request.pub().y()));
  if (!secret_reply.has_value()) {
    LOG(ERROR) << "Failed to get positive match secret.";
    reply.set_status(AuthenticateCredentialReply::NO_SECRET);
    std::move(callback).Run(std::move(reply));
    return;
  }

  reply.set_status(AuthenticateCredentialReply::SUCCESS);
  reply.set_encrypted_secret(
      brillo::BlobToString(secret_reply->encrypted_secret));
  reply.set_iv(brillo::BlobToString(secret_reply->iv));
  reply.mutable_pub()->set_x(brillo::BlobToString(secret_reply->pk_out_x));
  reply.mutable_pub()->set_y(brillo::BlobToString(secret_reply->pk_out_y));
  reply.set_record_id(metadata->record_id);

  std::move(callback).Run(std::move(reply));

  if (match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED) {
    UpdateDirtyTemplates();
  }

  return;
}

DeleteCredentialReply CrosFpAuthStackManager::DeleteCredential(
    const DeleteCredentialRequest& request) {
  DeleteCredentialReply reply;
  const std::optional<std::string>& current_user = session_manager_->GetUser();
  if (!current_user.has_value() || current_user.value() != request.user_id()) {
    if (!session_manager_->DeleteNotLoadedRecord(request.user_id(),
                                                 request.record_id())) {
      LOG(ERROR) << "Failed to delete credential.";
      reply.set_status(DeleteCredentialReply::DELETION_FAILED);
    } else {
      reply.set_status(DeleteCredentialReply::SUCCESS);
    }
    return reply;
  }
  const std::string& record_id = request.record_id();
  if (!session_manager_->HasRecordId(record_id)) {
    LOG(WARNING) << "Trying to delete a non-existing credential.";
    reply.set_status(DeleteCredentialReply::NOT_EXIST);
    return reply;
  }
  if (!session_manager_->DeleteRecord(record_id)) {
    LOG(ERROR) << "Failed to delete credential.";
    reply.set_status(DeleteCredentialReply::DELETION_FAILED);
    return reply;
  }
  if (!UploadCurrentUserTemplates()) {
    LOG(ERROR) << "Failed to reload the current user's templates. Biod locked "
                  "for further actions.";
    // The credential is still deleted successfully, so don't need to return
    // error here.
  }
  reply.set_status(DeleteCredentialReply::SUCCESS);
  return reply;
}

void CrosFpAuthStackManager::OnUserLoggedOut() {
  // Don't unload the user from session_manager_ and FPMCU. The user will be
  // brought back to login screen, where we'll have to load the records for them
  // again anyway. Save the unnecessary unload/load. If there are multiple users
  // on the device and another user is selected in the login screen, we'll still
  // be able to unload the current user and load the new user as
  // |locked_to_current_user_| is set to false;
  locked_to_current_user_ = false;
  migrator_->OnUserLoggedOut();
}

void CrosFpAuthStackManager::OnUserLoggedIn(const std::string& user_id) {
  LoadUser(user_id, true);
  migrator_->OnUserLoggedIn(user_id);
}

bool CrosFpAuthStackManager::SendStatsOnLogin() {
  size_t num_of_templates = session_manager_->GetNumOfTemplates();
  bool rc = true;
  rc = biod_metrics_->SendEnrolledFingerCount(num_of_templates) && rc;
  rc = biod_metrics_->SendFpUnlockEnabled(num_of_templates > 0) && rc;
  return rc;
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
  if (!next_session_action_.is_null()) {
    next_session_action_.Run(event);
  }
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
    ScanResult result, const AuthStackManager::EnrollStatus& enroll_status) {
  on_enroll_scan_done_.Run(result, enroll_status);
}

void CrosFpAuthStackManager::OnAuthScanDone() {
  on_auth_scan_done_.Run();
}

void CrosFpAuthStackManager::OnSessionFailed() {
  on_session_failed_.Run();
}

bool CrosFpAuthStackManager::LoadUser(std::string user_id, bool lock_to_user) {
  const std::optional<std::string>& current_user = session_manager_->GetUser();
  if (current_user.has_value() && current_user.value() == user_id) {
    // No action required, the user is already loaded.
    return true;
  } else if (current_user.has_value()) {
    if (locked_to_current_user_) {
      LOG(ERROR) << "Can't load another user as a user is logged-in.";
      return false;
    }
    session_manager_->UnloadUser();
  }
  // Any failure beyond this will lock the whole biod state machine.
  if (lock_to_user) {
    locked_to_current_user_ = true;
  }
  if (!session_manager_->LoadUser(std::move(user_id))) {
    LOG(ERROR) << "Failed to start user session.";
    state_ = State::kLocked;
    return false;
  }
  return UploadCurrentUserTemplates();
}

bool CrosFpAuthStackManager::UploadCurrentUserTemplates() {
  cros_dev_->ResetContext();
  const std::vector<CrosFpSessionManager::SessionRecord>& records =
      session_manager_->GetRecords();
  for (const auto& record : records) {
    biod_metrics_->SendRecordFormatVersion(
        record.record_metadata.record_format_version);
    LOG(INFO) << "Upload record " << LogSafeID(record.record_metadata.record_id)
              << ".";
    if (!cros_dev_->UploadTemplate(record.tmpl)) {
      LOG(ERROR) << "Upload template failed.";
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
  ++num_enrollment_captures_;

  if (percent < 100) {
    AuthStackManager::EnrollStatus enroll_status = {
        .done = false,
        .percent_complete = percent,
    };

    OnEnrollScanDone(scan_result, enroll_status);

    // The user needs to remove the finger before the next enrollment image.
    if (!RequestEnrollFingerUp()) {
      OnSessionFailed();
    }

    return;
  }

  OnTaskComplete();
  biod_metrics_->SendEnrollmentCapturesCount(num_enrollment_captures_);
  state_ = State::kEnrollDone;

  AuthStackManager::EnrollStatus enroll_status = {
      .done = true,
      .percent_complete = 100,
  };
  OnEnrollScanDone(ScanResult::SCAN_RESULT_SUCCESS, enroll_status);
}

void CrosFpAuthStackManager::DoEnrollFingerUpEvent(uint32_t event) {
  if (!(event & EC_MKBP_FP_FINGER_UP)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }

  if (!RequestEnrollImage()) {
    OnSessionFailed();
  }
}

StartAuthSessionStatus CrosFpAuthStackManager::PrepareStartAuthSession(
    const StartAuthSessionRequest& request) {
  if (!cros_dev_->SetNonceContext(
          brillo::BlobFromString(request.gsc_nonce()),
          brillo::BlobFromString(request.encrypted_label_seed()),
          brillo::BlobFromString(request.iv()))) {
    LOG(ERROR) << "Failed to set nonce context";
    return StartAuthSessionStatus::kSetContextFailed;
  }
  if (!cros_dev_->UnlockTemplates(session_manager_->GetNumOfTemplates())) {
    LOG(ERROR) << "Failed to unlock templates.";
    return StartAuthSessionStatus::kUnlockTemplatesFailed;
  }
  if (!RequestMatchFingerDown()) {
    return StartAuthSessionStatus::kSetMatchModeFailed;
  }
  return StartAuthSessionStatus::kSuccess;
}

bool CrosFpAuthStackManager::RequestMatchFingerDown() {
  next_session_action_ = base::BindRepeating(
      &CrosFpAuthStackManager::OnMatchFingerDown, base::Unretained(this));
  if (!cros_dev_->SetFpMode(ec::FpMode(Mode::kMatch))) {
    next_session_action_ = SessionAction();
    LOG(ERROR) << "Failed to start match mode";
    return false;
  }
  return true;
}

void CrosFpAuthStackManager::OnMatchFingerDown(uint32_t event) {
  if (!(event & EC_MKBP_FP_MATCH)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }

  pending_match_event_ = event;
  OnTaskComplete();
  state_ = State::kAuthDone;
  OnAuthScanDone();
}

bool CrosFpAuthStackManager::RequestFingerUp() {
  next_session_action_ = base::BindRepeating(
      &CrosFpAuthStackManager::OnFingerUpEvent, base::Unretained(this));
  if (!cros_dev_->SetFpMode(ec::FpMode(Mode::kFingerUp))) {
    next_session_action_ = SessionAction();
    LOG(ERROR) << "Failed to request finger up event";
    return false;
  }
  return true;
}

void CrosFpAuthStackManager::OnFingerUpEvent(uint32_t event) {
  if (!(event & EC_MKBP_FP_FINGER_UP)) {
    LOG(WARNING) << "Unexpected MKBP event: 0x" << std::hex << event;
    // Continue waiting for the proper event, do not abort session.
    return;
  }
  if (state_ == State::kWaitForFingerUp) {
    state_ = State::kNone;
  } else if (state_ == State::kAuthWaitForFingerUp) {
    std::optional<StartAuthSessionRequest> request;
    request.swap(pending_request_);
    if (!request.has_value()) {
      OnSessionFailed();
      state_ = State::kNone;
      return;
    }
    StartAuthSessionStatus status = PrepareStartAuthSession(*request);
    biod_metrics_->SendStartAuthSessionStatus(status);
    if (status != StartAuthSessionStatus::kSuccess) {
      LOG(ERROR) << "Failed to prepare start auth session";
      OnSessionFailed();
      state_ = State::kNone;
      return;
    }
    state_ = State::kAuth;
  } else {
    LOG(ERROR) << "Finger up event receiving in unexpected state: "
               << CurrentStateToString();
  }
}

void CrosFpAuthStackManager::ReportMatchLatency(bool matched) {
  std::optional<ec::CrosFpDeviceInterface::FpStats> stats =
      cros_dev_->GetFpStats();
  if (stats.has_value()) {
    biod_metrics_->SendFpLatencyStats(matched, *stats);
  }
}

void CrosFpAuthStackManager::UpdateDirtyTemplates() {
  std::vector<int> dirty_list = GetDirtyList(cros_dev_.get());
  for (int i : dirty_list) {
    std::unique_ptr<VendorTemplate> templ = cros_dev_->GetTemplate(i);
    if (!templ) {
      LOG(WARNING) << "Failed to get template with index " << i << ".";
      continue;
    }

    const std::optional<BiodStorageInterface::RecordMetadata> metadata =
        session_manager_->GetRecordMetadata(i);
    if (!metadata.has_value()) {
      LOG(WARNING) << "Can't find metadata for template index " << i << ".";
      continue;
    }

    if (!session_manager_->UpdateRecord(*metadata, std::move(templ))) {
      LOG(WARNING) << "Failed to update record with template index " << i
                   << ".";
      continue;
    }

    LOG(INFO) << "Successfully updated template index " << i << ".";
  }
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
    case State::kWaitForFingerUp:
      return "WaitForFingerUp";
    case State::kAuthWaitForFingerUp:
      return "AuthWaitForFingerUp";
    case State::kLocked:
      return "Locked";
  }
}

bool CrosFpAuthStackManager::IsActiveState() {
  switch (state_) {
    case State::kEnroll:
    case State::kAuth:
    case State::kWaitForFingerUp:
    case State::kAuthWaitForFingerUp:
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
    case State::kAuthDone:
    case State::kWaitForFingerUp:
      return true;
    case State::kEnroll:
    case State::kAuth:
    case State::kAuthWaitForFingerUp:
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
    case State::kWaitForFingerUp:
      return true;
    case State::kEnroll:
    case State::kAuth:
    case State::kLocked:
    case State::kAuthWaitForFingerUp:
      return false;
  }
}

bool CrosFpAuthStackManager::CanAuthenticateCredential() {
  return state_ == State::kAuthDone;
}

ListLegacyRecordsReply CrosFpAuthStackManager::ListLegacyRecords() {
  return migrator_->ListLegacyRecords();
}

void CrosFpAuthStackManager::EnrollLegacyTemplate(
    const EnrollLegacyTemplateRequest& request,
    AuthStackManager::EnrollLegacyTemplateCallback callback) {
  migrator_->EnrollLegacyTemplate(request, std::move(callback));
}

CrosFpAuthStackManager::CrosFpMigrator::CrosFpMigrator(
    CrosFpAuthStackManager* manager,
    std::unique_ptr<CrosFpSessionManager> session_manager)
    : manager_(manager), session_manager_(std::move(session_manager)) {
  CHECK(manager_);
  CHECK(session_manager_);
}

void CrosFpAuthStackManager::CrosFpMigrator::OnUserLoggedIn(
    const std::string& user_id) {
  session_manager_->LoadUser(user_id);
}

void CrosFpAuthStackManager::CrosFpMigrator::OnUserLoggedOut() {
  session_manager_->UnloadUser();
}

ListLegacyRecordsReply
CrosFpAuthStackManager::CrosFpMigrator::ListLegacyRecords() {
  ListLegacyRecordsReply reply;

  // Check if user is logged in.
  const std::optional<std::string>& user = session_manager_->GetUser();
  if (!user.has_value()) {
    LOG(WARNING) << "ListLegacyRecords called while user is not logged in.";
    reply.set_status(ListLegacyRecordsReply::INCORRECT_STATE);
    return reply;
  }
  const std::vector<CrosFpSessionManager::SessionRecord>& records =
      session_manager_->GetRecords();

  for (const auto& record : records) {
    LegacyRecord* legacy_record = reply.add_legacy_records();
    legacy_record->set_legacy_record_id(record.record_metadata.record_id);
    legacy_record->set_label(record.record_metadata.label);
  }
  reply.set_status(ListLegacyRecordsReply::SUCCESS);
  return reply;
}

void CrosFpAuthStackManager::CrosFpMigrator::EnrollLegacyTemplate(
    const EnrollLegacyTemplateRequest& request,
    AuthStackManager::EnrollLegacyTemplateCallback callback) {
  if (!manager_->CanStartEnroll()) {
    LOG(ERROR) << "Can't enroll legacy template now, current state is: "
               << manager_->CurrentStateToString();
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kIncorrectState);
    std::move(callback).Run(false);
    return;
  }

  const std::optional<std::string>& user_id = session_manager_->GetUser();
  if (!user_id.has_value()) {
    LOG(ERROR)
        << "Can only enroll legacy template when there is a user session.";
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kIncorrectState);
    std::move(callback).Run(false);
    return;
  }

  size_t num_of_templates = manager_->session_manager_->GetNumOfTemplates();
  if (num_of_templates >= manager_->cros_dev_->MaxTemplateCount()) {
    LOG(ERROR) << "No space for an additional template.";
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kTemplatesFull);
    std::move(callback).Run(false);
    return;
  }

  std::optional<CrosFpSessionManager::SessionRecord> legacy_record =
      session_manager_->GetRecordWithId(request.legacy_record_id());
  if (!legacy_record.has_value()) {
    LOG(ERROR) << "Can't find legacy record with id: "
               << request.legacy_record_id();
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kRecordNotFound);
    std::move(callback).Run(false);
    return;
  }

  if (!manager_->cros_dev_->SetNonceContext(
          brillo::BlobFromString(request.gsc_nonce()),
          brillo::BlobFromString(request.encrypted_label_seed()),
          brillo::BlobFromString(request.iv()))) {
    LOG(ERROR) << "Failed to set nonce context";
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kSetContextFailed);
    std::move(callback).Run(false);
    return;
  }

  if (!manager_->cros_dev_->UnlockTemplates(num_of_templates)) {
    LOG(ERROR) << "Failed to unlock templates.";
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kUnlockTemplatesFailed);
    std::move(callback).Run(false);
    return;
  }

  // Successful MigrateLegacyTemplate will leave the template in newly enrolled
  // state, which is identical to the state where StartEnrollSession finishes
  // and receives the enroll completed signal. The state machine is now
  // kEnrollDone and ready to handle a CreateCredential.
  if (!manager_->cros_dev_->MigrateLegacyTemplate(*user_id,
                                                  legacy_record->tmpl)) {
    LOG(ERROR) << "Failed to migrate legacy template.";
    manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
        EnrollLegacyTemplateStatus::kMigrateCommandFailed);
    std::move(callback).Run(false);
    return;
  }
  manager_->biod_metrics_->SendEnrollLegacyTemplateStatus(
      EnrollLegacyTemplateStatus::kSuccess);
  manager_->state_ = State::kEnrollDone;
  std::move(callback).Run(true);
}

}  // namespace biod
