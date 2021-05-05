// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
#include <chromeos/dbus/service_constants.h>
#include <openssl/rand.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/util.h"

namespace u2f {

namespace {

// User a big timeout for cryptohome. See b/172945202.
constexpr base::TimeDelta kCryptohomeTimeout = base::TimeDelta::FromMinutes(2);
constexpr int kVerificationTimeoutMs = 10000;
constexpr int kVerificationRetryDelayUs = 500 * 1000;
constexpr int kCancelUVFlowTimeoutMs = 5000;

// Cr50 Response codes.
// TODO(louiscollard): Don't duplicate these.
constexpr uint32_t kCr50StatusNotAllowed = 0x507;

constexpr char kAttestationFormatNone[] = "none";
// \xa0 is empty map in CBOR
constexpr char kAttestationStatementNone = '\xa0';
constexpr char kAttestationFormatU2f[] = "fido-u2f";
// Keys for attestation statement CBOR map.
constexpr char kSignatureKey[] = "sig";
constexpr char kX509CertKey[] = "x5c";

// The AAGUID for none-attestation (for platform-authenticator). For u2f/g2f
// attestation, empty AAGUID should be used.
const std::vector<uint8_t> kAaguid = {0x84, 0x03, 0x98, 0x77, 0xa5, 0x4b,
                                      0xdf, 0xbb, 0x04, 0xa8, 0x2d, 0xf2,
                                      0xfa, 0x2a, 0x11, 0x6e};

// AuthenticatorData flags are defined in
// https://www.w3.org/TR/webauthn-2/#sctn-authenticator-data
enum class AuthenticatorDataFlag : uint8_t {
  kTestOfUserPresence = 1u << 0,
  kTestOfUserVerification = 1u << 2,
  kAttestedCredentialData = 1u << 6,
  kExtensionDataIncluded = 1u << 7,
};

// COSE key parameters.
// https://tools.ietf.org/html/rfc8152#section-7.1
const int kCoseKeyKtyLabel = 1;
const int kCoseKeyKtyEC2 = 2;
const int kCoseKeyAlgLabel = 3;
const int kCoseKeyAlgES256 = -7;

// Double coordinate curve parameters.
// https://tools.ietf.org/html/rfc8152#section-13.1.1
const int kCoseECKeyCrvLabel = -1;
const int kCoseECKeyXLabel = -2;
const int kCoseECKeyYLabel = -3;

// Key label in cryptohome.
constexpr char kCryptohomePinLabel[] = "pin";

// Relative DBus object path for fingerprint manager in biod.
const char kCrosFpBiometricsManagerRelativePath[] = "/CrosFpBiometricsManager";

constexpr char kPerformingUserVerificationMetric[] =
    "WebAuthentication.ChromeOS.UserVerificationRequired";

std::vector<uint8_t> Uint16ToByteVector(uint16_t value) {
  return std::vector<uint8_t>({static_cast<uint8_t>((value >> 8) & 0xff),
                               static_cast<uint8_t>(value & 0xff)});
}

void AppendToString(const std::vector<uint8_t>& vect, std::string* str) {
  str->append(reinterpret_cast<const char*>(vect.data()), vect.size());
}

void AppendAttestedCredential(const std::vector<uint8_t>& credential_id,
                              const std::vector<uint8_t>& credential_public_key,
                              std::vector<uint8_t>* authenticator_data) {
  util::AppendToVector(credential_id, authenticator_data);
  util::AppendToVector(credential_public_key, authenticator_data);
}

// Returns the current time in seconds since epoch as a privacy-preserving
// signature counter. Because of the conversion to a 32-bit unsigned integer,
// the counter will overflow in the year 2108.
std::vector<uint8_t> GetTimestampSignatureCounter() {
  uint32_t sign_counter = static_cast<uint32_t>(base::Time::Now().ToDoubleT());
  return std::vector<uint8_t>{
      static_cast<uint8_t>((sign_counter >> 24) & 0xff),
      static_cast<uint8_t>((sign_counter >> 16) & 0xff),
      static_cast<uint8_t>((sign_counter >> 8) & 0xff),
      static_cast<uint8_t>(sign_counter & 0xff),
  };
}

std::vector<uint8_t> EncodeCredentialPublicKeyInCBOR(
    const std::vector<uint8_t>& credential_public_key) {
  DCHECK_EQ(credential_public_key.size(), sizeof(struct u2f_ec_point));
  cbor::Value::MapValue cbor_map;
  cbor_map[cbor::Value(kCoseKeyKtyLabel)] = cbor::Value(kCoseKeyKtyEC2);
  cbor_map[cbor::Value(kCoseKeyAlgLabel)] = cbor::Value(kCoseKeyAlgES256);
  cbor_map[cbor::Value(kCoseECKeyCrvLabel)] = cbor::Value(1);
  cbor_map[cbor::Value(kCoseECKeyXLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, x),
      U2F_EC_KEY_SIZE));
  cbor_map[cbor::Value(kCoseECKeyYLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, y),
      U2F_EC_KEY_SIZE));
  return *cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
}

std::vector<uint8_t> EncodeU2fAttestationStatementInCBOR(
    const std::vector<uint8_t>& signature, const std::vector<uint8_t>& cert) {
  cbor::Value::MapValue attestation_statement_map;
  attestation_statement_map[cbor::Value(kSignatureKey)] =
      cbor::Value(signature);
  // The "x5c" field is an array of just one cert.
  std::vector<cbor::Value> certificate_array;
  certificate_array.push_back(cbor::Value(cert));
  attestation_statement_map[cbor::Value(kX509CertKey)] =
      cbor::Value(std::move(certificate_array));
  return *cbor::Writer::Write(
      cbor::Value(std::move(attestation_statement_map)));
}

}  // namespace

WebAuthnHandler::WebAuthnHandler()
    : tpm_proxy_(nullptr),
      user_state_(nullptr),
      webauthn_storage_(std::make_unique<WebAuthnStorage>()) {}

void WebAuthnHandler::Initialize(
    dbus::Bus* bus,
    TpmVendorCommandProxy* tpm_proxy,
    UserState* user_state,
    U2fMode u2f_mode,
    std::function<void()> request_presence,
    std::unique_ptr<AllowlistingUtil> allowlisting_util,
    MetricsLibraryInterface* metrics) {
  if (Initialized()) {
    LOG(INFO) << "WebAuthn handler already initialized, doing nothing.";
    return;
  }

  metrics_ = metrics;
  tpm_proxy_ = tpm_proxy;
  user_state_ = user_state;
  user_state_->SetSessionStartedCallback(
      base::Bind(&WebAuthnHandler::OnSessionStarted, base::Unretained(this)));
  user_state_->SetSessionStoppedCallback(
      base::Bind(&WebAuthnHandler::OnSessionStopped, base::Unretained(this)));
  u2f_mode_ = u2f_mode;
  request_presence_ = request_presence;
  allowlisting_util_ = std::move(allowlisting_util);
  bus_ = bus;
  auth_dialog_dbus_proxy_ = bus_->GetObjectProxy(
      chromeos::kUserAuthenticationServiceName,
      dbus::ObjectPath(chromeos::kUserAuthenticationServicePath));
  // Testing can inject a mock.
  if (!cryptohome_proxy_)
    cryptohome_proxy_ =
        std::make_unique<org::chromium::CryptohomeInterfaceProxy>(bus_);
  DCHECK(auth_dialog_dbus_proxy_);

  if (user_state_->HasUser()) {
    // WebAuthnHandler should normally initialize on boot, before any user has
    // logged in. If there's already a user, then we have crashed during a user
    // session, so catch up on the state.
    base::Optional<std::string> user = user_state_->GetUser();
    DCHECK(user);
    OnSessionStarted(*user);
  }
}

bool WebAuthnHandler::Initialized() {
  return tpm_proxy_ != nullptr && user_state_ != nullptr;
}

bool WebAuthnHandler::AllowPresenceMode() {
  return u2f_mode_ == U2fMode::kU2f || u2f_mode_ == U2fMode::kU2fExtended;
}

void WebAuthnHandler::OnSessionStarted(const std::string& account_id) {
  // Do this first because there's a timeout for reading the secret.
  GetWebAuthnSecretAsync(account_id);

  webauthn_storage_->set_allow_access(true);
  base::Optional<std::string> sanitized_user = user_state_->GetSanitizedUser();
  DCHECK(sanitized_user);
  webauthn_storage_->set_sanitized_user(*sanitized_user);

  if (!webauthn_storage_->LoadRecords()) {
    LOG(ERROR) << "Did not load all records for user " << *sanitized_user;
    return;
  }
  webauthn_storage_->SendRecordCountToUMA(metrics_);
}

void WebAuthnHandler::OnSessionStopped() {
  auth_time_secret_hash_.reset();
  webauthn_storage_->Reset();
}

void WebAuthnHandler::GetWebAuthnSecretAsync(const std::string& account_id) {
  cryptohome::AccountIdentifier id;
  id.set_account_id(account_id);
  cryptohome::GetWebAuthnSecretRequest req;

  cryptohome_proxy_->GetWebAuthnSecretAsync(
      id, req,
      base::Bind(&WebAuthnHandler::OnGetWebAuthnSecretResp,
                 base::Unretained(this)),
      base::Bind(&WebAuthnHandler::OnGetWebAuthnSecretCallFailed,
                 base::Unretained(this)),
      kCryptohomeTimeout.InMilliseconds());
}

void WebAuthnHandler::OnGetWebAuthnSecretCallFailed(brillo::Error* error) {
  LOG(ERROR) << "Failed to call GetWebAuthnSecret on cryptohome, error: "
             << error->GetMessage();
}

void WebAuthnHandler::OnGetWebAuthnSecretResp(
    const cryptohome::BaseReply& reply) {
  // In case there's any error, read the backup hash first.
  auth_time_secret_hash_ = webauthn_storage_->LoadAuthTimeSecretHash();

  if (reply.has_error()) {
    LOG(ERROR) << "GetWebAuthnSecret reply has error " << reply.error();
    return;
  }

  if (!reply.HasExtension(cryptohome::GetWebAuthnSecretReply::reply)) {
    LOG(ERROR) << "GetWebAuthnSecret reply doesn't have the correct extension.";
    return;
  }

  brillo::SecureBlob secret(
      reply.GetExtension(cryptohome::GetWebAuthnSecretReply::reply)
          .webauthn_secret());
  if (secret.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "WebAuthn auth time secret size is wrong.";
    return;
  }

  std::unique_ptr<brillo::Blob> fresh_secret_hash =
      std::make_unique<brillo::Blob>(util::Sha256(secret));

  if (fresh_secret_hash) {
    // Persist to daemon-store in case we crash during a user session.
    webauthn_storage_->PersistAuthTimeSecretHash(*fresh_secret_hash);
    auth_time_secret_hash_ = std::move(fresh_secret_hash);
  }
}

void WebAuthnHandler::MakeCredential(
    std::unique_ptr<MakeCredentialMethodResponse> method_response,
    const MakeCredentialRequest& request) {
  MakeCredentialResponse response;

  if (!Initialized()) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    method_response->Return(response);
    return;
  }

  if (pending_uv_make_credential_session_ ||
      pending_uv_get_assertion_session_) {
    response.set_status(MakeCredentialResponse::REQUEST_PENDING);
    method_response->Return(response);
    return;
  }

  if (request.rp_id().empty()) {
    response.set_status(MakeCredentialResponse::INVALID_REQUEST);
    method_response->Return(response);
    return;
  }

  if (request.verification_type() == VerificationType::VERIFICATION_UNKNOWN) {
    response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    method_response->Return(response);
    return;
  }

  struct MakeCredentialSession session = {
      static_cast<uint64_t>(base::Time::Now().ToTimeT()), request,
      std::move(method_response)};

  if (!AllowPresenceMode()) {
    // Upgrade UP requests to UV.
    session.request.set_verification_type(
        VerificationType::VERIFICATION_USER_VERIFICATION);
  }

  if (session.request.verification_type() ==
      VerificationType::VERIFICATION_USER_VERIFICATION) {
    metrics_->SendBoolToUMA(kPerformingUserVerificationMetric, true);
    dbus::MethodCall call(
        chromeos::kUserAuthenticationServiceInterface,
        chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
    dbus::MessageWriter writer(&call);
    writer.AppendString(session.request.rp_id());
    writer.AppendInt32(session.request.verification_type());
    writer.AppendUint64(session.request.request_id());

    pending_uv_make_credential_session_ = std::move(session);
    auth_dialog_dbus_proxy_->CallMethod(
        &call, dbus::ObjectProxy::TIMEOUT_INFINITE,
        base::Bind(&WebAuthnHandler::HandleUVFlowResultMakeCredential,
                   base::Unretained(this)));
    return;
  }

  metrics_->SendBoolToUMA(kPerformingUserVerificationMetric, false);
  DoMakeCredential(std::move(session), PresenceRequirement::kPowerButton);
}

CancelWebAuthnFlowResponse WebAuthnHandler::Cancel(
    const CancelWebAuthnFlowRequest& request) {
  CancelWebAuthnFlowResponse response;
  if (!pending_uv_make_credential_session_ &&
      !pending_uv_get_assertion_session_) {
    LOG(ERROR) << "No pending session to cancel.";
    response.set_canceled(false);
    return response;
  }

  if (pending_uv_make_credential_session_ &&
      pending_uv_make_credential_session_->request.request_id() !=
          request.request_id()) {
    LOG(ERROR)
        << "MakeCredential session has a different request_id, not cancelling.";
    response.set_canceled(false);
    return response;
  }

  if (pending_uv_get_assertion_session_ &&
      pending_uv_get_assertion_session_->request.request_id() !=
          request.request_id()) {
    LOG(ERROR)
        << "GetAssertion session has a different request_id, not cancelling.";
    response.set_canceled(false);
    return response;
  }

  dbus::MethodCall call(chromeos::kUserAuthenticationServiceInterface,
                        chromeos::kUserAuthenticationServiceCancelMethod);
  std::unique_ptr<dbus::Response> cancel_ui_resp =
      auth_dialog_dbus_proxy_->CallMethodAndBlock(&call,
                                                  kCancelUVFlowTimeoutMs);

  if (!cancel_ui_resp) {
    LOG(ERROR) << "Failed to dismiss WebAuthn user verification UI.";
    response.set_canceled(false);
    return response;
  }

  // We do not reset |pending_uv_make_credential_session_| or
  // |pending_uv_get_assertion_session_| here because UI will still respond
  // to the cancelled request through these, though the response will be
  // ignored by Chrome.
  if (pending_uv_make_credential_session_) {
    pending_uv_make_credential_session_->canceled = true;
  } else {
    pending_uv_get_assertion_session_->canceled = true;
  }
  response.set_canceled(true);
  return response;
}

void WebAuthnHandler::HandleUVFlowResultMakeCredential(
    dbus::Response* flow_response) {
  MakeCredentialResponse response;

  DCHECK(pending_uv_make_credential_session_);

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  if (!success) {
    if (pending_uv_make_credential_session_->canceled) {
      LOG(INFO) << "WebAuthn MakeCredential operation canceled.";
      response.set_status(MakeCredentialResponse::CANCELED);
    } else {
      LOG(ERROR) << "User auth flow failed. Aborting MakeCredential.";
      response.set_status(MakeCredentialResponse::VERIFICATION_FAILED);
    }
    pending_uv_make_credential_session_->response->Return(response);
    pending_uv_make_credential_session_.reset();
    return;
  }

  DoMakeCredential(std::move(*pending_uv_make_credential_session_),
                   PresenceRequirement::kNone);
  pending_uv_make_credential_session_.reset();
}

void WebAuthnHandler::HandleUVFlowResultGetAssertion(
    dbus::Response* flow_response) {
  GetAssertionResponse response;

  DCHECK(pending_uv_get_assertion_session_);

  if (!flow_response) {
    LOG(ERROR) << "User auth flow had no response.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  dbus::MessageReader response_reader(flow_response);
  bool success;
  if (!response_reader.PopBool(&success)) {
    LOG(ERROR) << "Failed to parse user auth flow result.";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  if (!success) {
    if (pending_uv_get_assertion_session_->canceled) {
      LOG(INFO) << "WebAuthn GetAssertion operation canceled.";
      response.set_status(GetAssertionResponse::CANCELED);
    } else {
      LOG(ERROR) << "User auth flow failed. Aborting GetAssertion.";
      response.set_status(GetAssertionResponse::VERIFICATION_FAILED);
    }
    pending_uv_get_assertion_session_->response->Return(response);
    pending_uv_get_assertion_session_.reset();
    return;
  }

  DoGetAssertion(std::move(*pending_uv_get_assertion_session_),
                 PresenceRequirement::kAuthorizationSecret);
  pending_uv_get_assertion_session_.reset();
}

void WebAuthnHandler::DoMakeCredential(
    struct MakeCredentialSession session,
    PresenceRequirement presence_requirement) {
  MakeCredentialResponse response;
  const std::vector<uint8_t> rp_id_hash = util::Sha256(session.request.rp_id());
  std::vector<uint8_t> credential_id;
  std::vector<uint8_t> credential_public_key;

  // If we are in u2f or g2f mode, and the request says it wants presence only,
  // make a non-versioned (i.e. non-uv-compatible) credential.
  bool uv_compatible = !(AllowPresenceMode() &&
                         session.request.verification_type() ==
                             VerificationType::VERIFICATION_USER_PRESENCE);

  brillo::Blob credential_secret(kCredentialSecretSize);
  if (uv_compatible) {
    if (RAND_bytes(credential_secret.data(), credential_secret.size()) != 1) {
      LOG(ERROR) << "Failed to generate secret for new credential.";
      response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
      session.response->Return(response);
      return;
    }
  } else {
    // We are creating a credential that can only be signed with power button
    // press, and can be signed by u2f/g2f, so we must use the legacy secret.
    base::Optional<brillo::SecureBlob> legacy_secret =
        user_state_->GetUserSecret();
    if (!legacy_secret) {
      LOG(ERROR) << "Cannot find user secret when trying to create u2f/g2f "
                    "credential.";
      response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
      session.response->Return(response);
      return;
    }
    credential_secret =
        std::vector<uint8_t>(legacy_secret->begin(), legacy_secret->end());
  }

  MakeCredentialResponse::MakeCredentialStatus generate_status =
      DoU2fGenerate(rp_id_hash, credential_secret, presence_requirement,
                    uv_compatible, &credential_id, &credential_public_key);

  if (generate_status != MakeCredentialResponse::SUCCESS) {
    response.set_status(generate_status);
    session.response->Return(response);
    return;
  }

  if (credential_id.empty() || credential_public_key.empty()) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }

  if (uv_compatible)
    InsertAuthTimeSecretHashToCredentialId(&credential_id);

  auto ret = HasExcludedCredentials(session.request);
  if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  } else if (ret == HasCredentialsResponse::SUCCESS) {
    response.set_status(MakeCredentialResponse::EXCLUDED_CREDENTIAL_ID);
    session.response->Return(response);
    return;
  }

  const base::Optional<std::vector<uint8_t>> authenticator_data =
      MakeAuthenticatorData(
          rp_id_hash, credential_id,
          EncodeCredentialPublicKeyInCBOR(credential_public_key),
          /* user_verified = */ session.request.verification_type() ==
              VerificationType::VERIFICATION_USER_VERIFICATION,
          /* include_attested_credential_data = */ true,
          /* is_u2f_authenticator_credential = */ !uv_compatible);
  if (!authenticator_data) {
    LOG(ERROR) << "MakeAuthenticatorData failed";
    response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }
  AppendToString(*authenticator_data, response.mutable_authenticator_data());

  // If a credential is not UV-compatible, it is a legacy U2F/G2F credential
  // and should come with U2F/G2F attestation for backward compatibility.
  if (uv_compatible) {
    AppendNoneAttestation(&response);
  } else {
    const std::vector<uint8_t> data_to_sign =
        util::BuildU2fRegisterResponseSignedData(
            rp_id_hash, util::ToVector(session.request.client_data_hash()),
            credential_public_key, credential_id);
    base::Optional<std::vector<uint8_t>> attestation_statement =
        MakeFidoU2fAttestationStatement(
            data_to_sign, session.request.attestation_conveyance_preference());
    if (!attestation_statement) {
      response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
      session.response->Return(response);
      return;
    }
    response.set_attestation_format(kAttestationFormatU2f);
    AppendToString(*attestation_statement,
                   response.mutable_attestation_statement());
  }

  // u2f/g2f credentials should not be written to record.
  if (uv_compatible) {
    // All steps succeeded, so write to record.
    WebAuthnRecord record;
    AppendToString(credential_id, &record.credential_id);
    record.secret = std::move(credential_secret);
    record.rp_id = session.request.rp_id();
    record.rp_display_name = session.request.rp_display_name();
    record.user_id = session.request.user_id();
    record.user_display_name = session.request.user_display_name();
    record.timestamp = base::Time::Now().ToDoubleT();
    record.is_resident_key = session.request.resident_key_required();
    if (!webauthn_storage_->WriteRecord(std::move(record))) {
      response.set_status(MakeCredentialResponse::INTERNAL_ERROR);
      session.response->Return(response);
      return;
    }
  }

  response.set_status(MakeCredentialResponse::SUCCESS);
  session.response->Return(response);
}

// AuthenticatorData layout:
// (See https://www.w3.org/TR/webauthn-2/#table-authData)
// -----------------------------------------------------------------------
// | RP ID hash:       32 bytes
// | Flags:             1 byte
// | Signature counter: 4 bytes
// |                           -------------------------------------------
// |                           | AAGUID:                  16 bytes
// | Attested Credential Data: | Credential ID length (L): 2 bytes
// | (if present)              | Credential ID:            L bytes
// |                           | Credential public key:    variable length
base::Optional<std::vector<uint8_t>> WebAuthnHandler::MakeAuthenticatorData(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_public_key,
    bool user_verified,
    bool include_attested_credential_data,
    bool is_u2f_authenticator_credential) {
  std::vector<uint8_t> authenticator_data(rp_id_hash);
  uint8_t flags =
      static_cast<uint8_t>(AuthenticatorDataFlag::kTestOfUserPresence);
  if (user_verified)
    flags |=
        static_cast<uint8_t>(AuthenticatorDataFlag::kTestOfUserVerification);
  if (include_attested_credential_data)
    flags |=
        static_cast<uint8_t>(AuthenticatorDataFlag::kAttestedCredentialData);
  authenticator_data.emplace_back(flags);

  // The U2F authenticator keeps a user-global signature counter in UserState.
  // For platform authenticator credentials, we derive a counter from a
  // timestamp instead.
  if (is_u2f_authenticator_credential) {
    base::Optional<std::vector<uint8_t>> counter = user_state_->GetCounter();
    if (!counter || !user_state_->IncrementCounter()) {
      // UserState logs an error in this case.
      return base::nullopt;
    }
    util::AppendToVector(*counter, &authenticator_data);
  } else {
    util::AppendToVector(GetTimestampSignatureCounter(), &authenticator_data);
  }

  if (include_attested_credential_data) {
    util::AppendToVector(is_u2f_authenticator_credential
                             ? std::vector<uint8_t>(kAaguid.size(), 0)
                             : kAaguid,
                         &authenticator_data);
    uint16_t length = credential_id.size();
    util::AppendToVector(Uint16ToByteVector(length), &authenticator_data);

    AppendAttestedCredential(credential_id, credential_public_key,
                             &authenticator_data);
  }

  return authenticator_data;
}

void WebAuthnHandler::AppendNoneAttestation(MakeCredentialResponse* response) {
  response->set_attestation_format(kAttestationFormatNone);
  response->mutable_attestation_statement()->push_back(
      kAttestationStatementNone);
}

base::Optional<std::vector<uint8_t>>
WebAuthnHandler::MakeFidoU2fAttestationStatement(
    const std::vector<uint8_t>& data_to_sign,
    const MakeCredentialRequest::AttestationConveyancePreference
        attestation_conveyance_preference) {
  std::vector<uint8_t> attestation_cert;
  std::vector<uint8_t> signature;
  if (attestation_conveyance_preference == MakeCredentialRequest::G2F &&
      u2f_mode_ == U2fMode::kU2fExtended) {
    base::Optional<std::vector<uint8_t>> g2f_cert =
        util::GetG2fCert(tpm_proxy_);
    if (g2f_cert.has_value()) {
      attestation_cert = *g2f_cert;
    } else {
      LOG(ERROR) << "Failed to get G2f cert for MakeCredential";
      return base::nullopt;
    }

    MakeCredentialResponse::MakeCredentialStatus attest_status =
        DoG2fAttest(data_to_sign, U2F_ATTEST_FORMAT_REG_RESP, &signature);

    if (attest_status != MakeCredentialResponse::SUCCESS) {
      LOG(ERROR) << "Failed to do G2f attestation for MakeCredential";
      return base::nullopt;
    }

    if (allowlisting_util_ != nullptr &&
        !allowlisting_util_->AppendDataToCert(&attestation_cert)) {
      LOG(ERROR) << "Failed to get allowlisting data for G2F Enroll Request";
      return base::nullopt;
    }
  } else {
    if (!util::DoSoftwareAttest(data_to_sign, &attestation_cert, &signature)) {
      LOG(ERROR) << "Failed to do software attestation for MakeCredential";
      return base::nullopt;
    }
  }

  return EncodeU2fAttestationStatementInCBOR(signature, attestation_cert);
}

void WebAuthnHandler::CallAndWaitForPresence(std::function<uint32_t()> fn,
                                             uint32_t* status) {
  *status = fn();
  base::TimeTicks verification_start = base::TimeTicks::Now();
  while (*status == kCr50StatusNotAllowed &&
         base::TimeTicks::Now() - verification_start <
             base::TimeDelta::FromMilliseconds(kVerificationTimeoutMs)) {
    // We need user presence. Show a notification requesting it, and try again.
    request_presence_();
    usleep(kVerificationRetryDelayUs);
    *status = fn();
  }
}

MakeCredentialResponse::MakeCredentialStatus WebAuthnHandler::DoU2fGenerate(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_secret,
    PresenceRequirement presence_requirement,
    bool uv_compatible,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  struct u2f_generate_req generate_req = {};
  if (!util::VectorToObject(rp_id_hash, generate_req.appId,
                            sizeof(generate_req.appId))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }
  if (!util::VectorToObject(credential_secret, generate_req.userSecret,
                            sizeof(generate_req.userSecret))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }

  if (uv_compatible) {
    if (!auth_time_secret_hash_) {
      LOG(ERROR) << "No auth-time secret hash to use for u2f_generate.";
      return MakeCredentialResponse::INTERNAL_ERROR;
    }
    generate_req.flags |= U2F_UV_ENABLED_KH;
    memcpy(generate_req.authTimeSecretHash, auth_time_secret_hash_->data(),
           auth_time_secret_hash_->size());
    struct u2f_generate_versioned_resp generate_resp = {};

    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t generate_status =
          tpm_proxy_->SendU2fGenerate(generate_req, &generate_resp);
      if (generate_status != 0)
        return MakeCredentialResponse::INTERNAL_ERROR;

      util::AppendToVector(generate_resp.pubKey, credential_public_key);
      util::AppendToVector(generate_resp.keyHandle, credential_id);
      return MakeCredentialResponse::SUCCESS;
    }

    // Require user presence, consume.
    generate_req.flags |= U2F_AUTH_ENFORCE;
    return SendU2fGenerateWaitForPresence(&generate_req, &generate_resp,
                                          credential_id, credential_public_key);
  } else {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return MakeCredentialResponse::INTERNAL_ERROR;
    // Require user presence, consume.
    generate_req.flags |= U2F_AUTH_ENFORCE;
    struct u2f_generate_resp generate_resp = {};
    return SendU2fGenerateWaitForPresence(&generate_req, &generate_resp,
                                          credential_id, credential_public_key);
  }
}

template <typename Response>
MakeCredentialResponse::MakeCredentialStatus
WebAuthnHandler::SendU2fGenerateWaitForPresence(
    struct u2f_generate_req* generate_req,
    Response* generate_resp,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  uint32_t generate_status = -1;
  base::AutoLock(tpm_proxy_->GetLock());
  CallAndWaitForPresence(
      [this, generate_req, generate_resp]() {
        return tpm_proxy_->SendU2fGenerate(*generate_req, generate_resp);
      },
      &generate_status);
  brillo::SecureClearContainer(generate_req->userSecret);

  if (generate_status == 0) {
    util::AppendToVector(generate_resp->pubKey, credential_public_key);
    util::AppendToVector(generate_resp->keyHandle, credential_id);
    return MakeCredentialResponse::SUCCESS;
  }

  return MakeCredentialResponse::VERIFICATION_FAILED;
}

// TODO(b/172971998): Remove this workaround once cr50 handles this.
void WebAuthnHandler::InsertAuthTimeSecretHashToCredentialId(
    std::vector<uint8_t>* input) {
  CHECK(input->size() == sizeof(u2f_versioned_key_handle));
  // The auth time secret hash should be inserted right after the header and
  // the authorization salt, before the authorization hmac.
  input->insert(
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac),
      auth_time_secret_hash_->cbegin(), auth_time_secret_hash_->cend());
}

// TODO(b/172971998): Remove this workaround once cr50 handles this.
void WebAuthnHandler::RemoveAuthTimeSecretHashFromCredentialId(
    std::vector<uint8_t>* input) {
  CHECK_EQ(input->size(),
           sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_LENGTH);
  // The auth time secret hash is after the header and the authorization salt,
  // before the authorization hmac. Remove it so that cr50 recognizes the KH.
  const std::vector<uint8_t>::const_iterator remove_begin =
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac);
  input->erase(remove_begin, remove_begin + SHA256_DIGEST_LENGTH);
}

HasCredentialsResponse::HasCredentialsStatus
WebAuthnHandler::HasExcludedCredentials(const MakeCredentialRequest& request) {
  MatchedCredentials matched =
      FindMatchedCredentials(request.excluded_credential_id(), request.rp_id(),
                             request.app_id_exclude());
  if (matched.has_internal_error) {
    return HasCredentialsResponse::INTERNAL_ERROR;
  }

  if (matched.platform_credentials.empty() &&
      matched.legacy_credentials_for_rp_id.empty() &&
      matched.legacy_credentials_for_app_id.empty()) {
    return HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
  }
  return HasCredentialsResponse::SUCCESS;
}

void WebAuthnHandler::GetAssertion(
    std::unique_ptr<GetAssertionMethodResponse> method_response,
    const GetAssertionRequest& request) {
  GetAssertionResponse response;

  if (!Initialized()) {
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    method_response->Return(response);
    return;
  }

  if (pending_uv_make_credential_session_ ||
      pending_uv_get_assertion_session_) {
    response.set_status(GetAssertionResponse::REQUEST_PENDING);
    method_response->Return(response);
    return;
  }

  if (request.rp_id().empty() ||
      request.client_data_hash().size() != SHA256_DIGEST_LENGTH) {
    response.set_status(GetAssertionResponse::INVALID_REQUEST);
    method_response->Return(response);
    return;
  }

  if (request.verification_type() == VerificationType::VERIFICATION_UNKNOWN) {
    response.set_status(GetAssertionResponse::VERIFICATION_FAILED);
    method_response->Return(response);
    return;
  }

  // TODO(louiscollard): Support resident credentials.

  std::string* credential_to_use;
  bool is_legacy_credential = false;
  bool use_app_id = false;

  MatchedCredentials matched = FindMatchedCredentials(
      request.allowed_credential_id(), request.rp_id(), request.app_id());
  if (matched.has_internal_error) {
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    method_response->Return(response);
    return;
  }

  if (!matched.platform_credentials.empty()) {
    credential_to_use = &matched.platform_credentials[0];
  } else if (!matched.legacy_credentials_for_rp_id.empty()) {
    credential_to_use = &matched.legacy_credentials_for_rp_id[0];
    is_legacy_credential = true;
  } else if (!matched.legacy_credentials_for_app_id.empty()) {
    credential_to_use = &matched.legacy_credentials_for_app_id[0];
    is_legacy_credential = true;
    use_app_id = true;
  } else {
    response.set_status(GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
    method_response->Return(response);
    return;
  }

  struct GetAssertionSession session = {
      static_cast<uint64_t>(base::Time::Now().ToTimeT()), request,
      *credential_to_use, std::move(method_response)};
  if (use_app_id) {
    // App id was matched instead of rp id, so discard rp id.
    session.request.set_rp_id(request.app_id());
  }

  if (!AllowPresenceMode()) {
    // Upgrade UP requests to UV.
    session.request.set_verification_type(
        VerificationType::VERIFICATION_USER_VERIFICATION);
  }

  // Legacy credentials should go through power button, not UV.
  if (session.request.verification_type() ==
          VerificationType::VERIFICATION_USER_VERIFICATION &&
      !is_legacy_credential) {
    metrics_->SendBoolToUMA(kPerformingUserVerificationMetric, true);
    dbus::MethodCall call(
        chromeos::kUserAuthenticationServiceInterface,
        chromeos::kUserAuthenticationServiceShowAuthDialogMethod);
    dbus::MessageWriter writer(&call);
    writer.AppendString(session.request.rp_id());
    writer.AppendInt32(session.request.verification_type());
    writer.AppendUint64(session.request.request_id());

    pending_uv_get_assertion_session_ = std::move(session);
    auth_dialog_dbus_proxy_->CallMethod(
        &call, dbus::ObjectProxy::TIMEOUT_INFINITE,
        base::Bind(&WebAuthnHandler::HandleUVFlowResultGetAssertion,
                   base::Unretained(this)));
    return;
  }

  metrics_->SendBoolToUMA(kPerformingUserVerificationMetric, false);
  DoGetAssertion(std::move(session), PresenceRequirement::kPowerButton);
}

// If already seeing failure, then no need to get user secret. This means
// in the fingerprint case, this signal should ideally come from UI instead of
// biod because only UI knows about retry.
void WebAuthnHandler::DoGetAssertion(struct GetAssertionSession session,
                                     PresenceRequirement presence_requirement) {
  GetAssertionResponse response;

  bool is_u2f_authenticator_credential = false;
  base::Optional<std::vector<uint8_t>> credential_secret =
      webauthn_storage_->GetSecretByCredentialId(session.credential_id);
  if (!credential_secret) {
    if (!AllowPresenceMode()) {
      LOG(ERROR) << "No credential secret for credential id "
                 << session.credential_id << ", aborting GetAssertion.";
      response.set_status(GetAssertionResponse::UNKNOWN_CREDENTIAL_ID);
      session.response->Return(response);
      return;
    }

    // Maybe signing u2fhid credentials. Use legacy secret instead.
    base::Optional<brillo::SecureBlob> legacy_secret =
        user_state_->GetUserSecret();
    if (!legacy_secret) {
      LOG(ERROR)
          << "Cannot find user secret when trying to sign u2fhid credentials";
      response.set_status(GetAssertionResponse::INTERNAL_ERROR);
      session.response->Return(response);
      return;
    }
    credential_secret =
        std::vector<uint8_t>(legacy_secret->begin(), legacy_secret->end());
    is_u2f_authenticator_credential = true;
  }

  const std::vector<uint8_t> rp_id_hash = util::Sha256(session.request.rp_id());
  const base::Optional<std::vector<uint8_t>> authenticator_data =
      MakeAuthenticatorData(
          rp_id_hash, std::vector<uint8_t>(), std::vector<uint8_t>(),
          // If presence requirement is "power button" then the user was not
          // verified. Otherwise the user was verified through UI.
          /* user_verified = */ presence_requirement !=
              PresenceRequirement::kPowerButton,
          /* include_attested_credential_data = */ false,
          is_u2f_authenticator_credential);
  if (!authenticator_data) {
    LOG(ERROR) << "MakeAuthenticatorData failed";
    response.set_status(GetAssertionResponse::INTERNAL_ERROR);
    session.response->Return(response);
    return;
  }

  std::vector<uint8_t> data_to_sign(*authenticator_data);
  util::AppendToVector(session.request.client_data_hash(), &data_to_sign);
  std::vector<uint8_t> hash_to_sign = util::Sha256(data_to_sign);

  std::vector<uint8_t> signature;
  GetAssertionResponse::GetAssertionStatus sign_status =
      DoU2fSign(rp_id_hash, hash_to_sign, util::ToVector(session.credential_id),
                *credential_secret, presence_requirement, &signature);
  response.set_status(sign_status);
  if (sign_status == GetAssertionResponse::SUCCESS) {
    auto* assertion = response.add_assertion();
    assertion->set_credential_id(session.credential_id);
    AppendToString(*authenticator_data,
                   assertion->mutable_authenticator_data());
    AppendToString(signature, assertion->mutable_signature());
  }

  session.response->Return(response);
}

GetAssertionResponse::GetAssertionStatus WebAuthnHandler::DoU2fSign(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& hash_to_sign,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret,
    PresenceRequirement presence_requirement,
    std::vector<uint8_t>* signature) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  if (credential_id.size() ==
      sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_SIZE) {
    // Allow waiving presence if sign_req.authTimeSecret is correct.
    struct u2f_sign_versioned_req sign_req = {};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    struct u2f_sign_resp sign_resp = {};

    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
      if (sign_status != 0)
        return GetAssertionResponse::INTERNAL_ERROR;

      base::Optional<std::vector<uint8_t>> opt_signature =
          util::SignatureToDerBytes(sign_resp.sig_r, sign_resp.sig_s);
      if (!opt_signature.has_value()) {
        return GetAssertionResponse::INTERNAL_ERROR;
      }
      *signature = *opt_signature;
      return GetAssertionResponse::SUCCESS;
    }

    // Require user presence, consume.
    sign_req.flags |= U2F_AUTH_ENFORCE;
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else if (credential_id.size() == sizeof(u2f_key_handle)) {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return GetAssertionResponse::INTERNAL_ERROR;

    struct u2f_sign_req sign_req = {
        .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
    };
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp = {};
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else {
    return GetAssertionResponse::INVALID_REQUEST;
  }
}

template <typename Request>
GetAssertionResponse::GetAssertionStatus
WebAuthnHandler::SendU2fSignWaitForPresence(Request* sign_req,
                                            struct u2f_sign_resp* sign_resp,
                                            std::vector<uint8_t>* signature) {
  uint32_t sign_status = -1;
  base::AutoLock(tpm_proxy_->GetLock());
  CallAndWaitForPresence(
      [this, sign_req, sign_resp]() {
        return tpm_proxy_->SendU2fSign(*sign_req, sign_resp);
      },
      &sign_status);
  brillo::SecureClearContainer(sign_req->userSecret);

  if (sign_status == 0) {
    base::Optional<std::vector<uint8_t>> opt_signature =
        util::SignatureToDerBytes(sign_resp->sig_r, sign_resp->sig_s);
    if (!opt_signature.has_value()) {
      return GetAssertionResponse::INTERNAL_ERROR;
    }
    *signature = *opt_signature;
    return GetAssertionResponse::SUCCESS;
  }

  return GetAssertionResponse::VERIFICATION_FAILED;
}

MakeCredentialResponse::MakeCredentialStatus WebAuthnHandler::DoG2fAttest(
    const std::vector<uint8_t>& data,
    uint8_t format,
    std::vector<uint8_t>* signature_out) {
  base::AutoLock(tpm_proxy_->GetLock());
  base::Optional<brillo::SecureBlob> user_secret = user_state_->GetUserSecret();
  if (!user_secret.has_value()) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  struct u2f_attest_req attest_req = {
      .format = format, .dataLen = static_cast<uint8_t>(data.size())};
  if (!util::VectorToObject(*user_secret, attest_req.userSecret,
                            sizeof(attest_req.userSecret))) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }
  if (!util::VectorToObject(data, attest_req.data, sizeof(attest_req.data))) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  struct u2f_attest_resp attest_resp = {};
  uint32_t attest_status = tpm_proxy_->SendU2fAttest(attest_req, &attest_resp);

  brillo::SecureClearBytes(&attest_req.userSecret,
                           sizeof(attest_req.userSecret));

  if (attest_status != 0) {
    // We are attesting to a key handle that we just created, so if
    // attestation fails we have hit some internal error.
    LOG(ERROR) << "U2F_ATTEST failed, status: " << std::hex
               << static_cast<uint32_t>(attest_status);
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  base::Optional<std::vector<uint8_t>> signature =
      util::SignatureToDerBytes(attest_resp.sig_r, attest_resp.sig_s);

  if (!signature.has_value()) {
    LOG(ERROR) << "DER encoding of U2F_ATTEST signature failed.";
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  *signature_out = *signature;

  return MakeCredentialResponse::SUCCESS;
}

MatchedCredentials WebAuthnHandler::FindMatchedCredentials(
    const RepeatedPtrField<std::string>& all_credentials,
    const std::string& rp_id,
    const std::string& app_id) {
  std::vector<uint8_t> rp_id_hash = util::Sha256(rp_id);
  std::vector<uint8_t> app_id_hash = util::Sha256(app_id);
  MatchedCredentials result;

  // Platform authenticator credentials.
  for (const auto& credential_id : all_credentials) {
    base::Optional<std::vector<uint8_t>> credential_secret =
        webauthn_storage_->GetSecretByCredentialId(credential_id);
    if (!credential_secret)
      continue;

    auto ret = DoU2fSignCheckOnly(rp_id_hash, util::ToVector(credential_id),
                                  *credential_secret);
    if (ret == HasCredentialsResponse::INTERNAL_ERROR) {
      result.has_internal_error = true;
      return result;
    } else if (ret == HasCredentialsResponse::SUCCESS) {
      result.platform_credentials.emplace_back(credential_id);
    }
  }

  const base::Optional<brillo::SecureBlob> user_secret =
      user_state_->GetUserSecret();
  if (!user_secret) {
    result.has_internal_error = true;
    return result;
  }

  // Legacy credentials. If a legacy credential matches both rp_id and app_id,
  // it will only appear in result.legacy_credentials_for_rp_id.
  for (const auto& credential_id : all_credentials) {
    // First try matching rp_id.
    HasCredentialsResponse::HasCredentialsStatus ret = DoU2fSignCheckOnly(
        rp_id_hash, util::ToVector(credential_id),
        std::vector<uint8_t>(user_secret->begin(), user_secret->end()));
    DCHECK(HasCredentialsResponse::HasCredentialsStatus_IsValid(ret));
    switch (ret) {
      case HasCredentialsResponse::SUCCESS:
        // rp_id matched, it's a credential registered with u2fhid on WebAuthn
        // API.
        result.legacy_credentials_for_rp_id.emplace_back(credential_id);
        continue;
      case HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID:
        break;
      case HasCredentialsResponse::UNKNOWN:
      case HasCredentialsResponse::INVALID_REQUEST:
      case HasCredentialsResponse::INTERNAL_ERROR:
        result.has_internal_error = true;
        return result;
      case google::protobuf::kint32min:
      case google::protobuf::kint32max:
        NOTREACHED();
    }

    // Try matching app_id.
    ret = DoU2fSignCheckOnly(
        app_id_hash, util::ToVector(credential_id),
        std::vector<uint8_t>(user_secret->begin(), user_secret->end()));
    DCHECK(HasCredentialsResponse::HasCredentialsStatus_IsValid(ret));
    switch (ret) {
      case HasCredentialsResponse::SUCCESS:
        // App id extension matched. It's a legacy credential registered with
        // the U2F interface.
        result.legacy_credentials_for_app_id.emplace_back(credential_id);
        continue;
      case HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID:
        break;
      case HasCredentialsResponse::UNKNOWN:
      case HasCredentialsResponse::INVALID_REQUEST:
      case HasCredentialsResponse::INTERNAL_ERROR:
        result.has_internal_error = true;
        return result;
      case google::protobuf::kint32min:
      case google::protobuf::kint32max:
        NOTREACHED();
    }
  }

  return result;
}

HasCredentialsResponse WebAuthnHandler::HasCredentials(
    const HasCredentialsRequest& request) {
  HasCredentialsResponse response;

  if (!Initialized()) {
    response.set_status(HasCredentialsResponse::INTERNAL_ERROR);
    return response;
  }

  if (request.rp_id().empty() || request.credential_id().empty()) {
    response.set_status(HasCredentialsResponse::INVALID_REQUEST);
    return response;
  }

  MatchedCredentials matched = FindMatchedCredentials(
      request.credential_id(), request.rp_id(), request.app_id());
  if (matched.has_internal_error) {
    response.set_status(HasCredentialsResponse::INTERNAL_ERROR);
    return response;
  }

  for (const auto& credential_id : matched.platform_credentials) {
    *response.add_credential_id() = credential_id;
  }
  for (const auto& credential_id : matched.legacy_credentials_for_rp_id) {
    *response.add_credential_id() = credential_id;
  }
  for (const auto& credential_id : matched.legacy_credentials_for_app_id) {
    *response.add_credential_id() = credential_id;
  }

  response.set_status((response.credential_id_size() > 0)
                          ? HasCredentialsResponse::SUCCESS
                          : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
  return response;
}

HasCredentialsResponse WebAuthnHandler::HasLegacyCredentials(
    const HasCredentialsRequest& request) {
  HasCredentialsResponse response;

  if (!Initialized()) {
    response.set_status(HasCredentialsResponse::INTERNAL_ERROR);
    return response;
  }

  if (request.credential_id().empty()) {
    response.set_status(HasCredentialsResponse::INVALID_REQUEST);
    return response;
  }

  MatchedCredentials matched = FindMatchedCredentials(
      request.credential_id(), request.rp_id(), request.app_id());
  if (matched.has_internal_error) {
    response.set_status(HasCredentialsResponse::INTERNAL_ERROR);
    return response;
  }

  // Do not include platform credentials.
  for (const auto& credential_id : matched.legacy_credentials_for_rp_id) {
    *response.add_credential_id() = credential_id;
  }
  for (const auto& credential_id : matched.legacy_credentials_for_app_id) {
    *response.add_credential_id() = credential_id;
  }

  response.set_status((response.credential_id_size() > 0)
                          ? HasCredentialsResponse::SUCCESS
                          : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID);
  return response;
}

HasCredentialsResponse::HasCredentialsStatus
WebAuthnHandler::DoU2fSignCheckOnly(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret) {
  uint32_t sign_status;

  if (credential_id.size() ==
      sizeof(u2f_versioned_key_handle) + SHA256_DIGEST_SIZE) {
    struct u2f_sign_versioned_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClearContainer(sign_req.userSecret);
  } else if (credential_id.size() == sizeof(u2f_key_handle)) {
    struct u2f_sign_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;
    base::AutoLock(tpm_proxy_->GetLock());
    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClearContainer(sign_req.userSecret);
  } else {
    return HasCredentialsResponse::INVALID_REQUEST;
  }

  // Return status of 0 indicates the credential is valid.
  return (sign_status == 0) ? HasCredentialsResponse::SUCCESS
                            : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
}

IsU2fEnabledResponse WebAuthnHandler::IsU2fEnabled(
    const IsU2fEnabledRequest& request) {
  IsU2fEnabledResponse response;
  response.set_enabled(AllowPresenceMode());
  return response;
}

void WebAuthnHandler::IsUvpaa(
    std::unique_ptr<IsUvpaaMethodResponse> method_response,
    const IsUvpaaRequest& request) {
  // Checking with the authentication dialog (in Ash) will not work, because
  // currently in Chrome the IsUvpaa is a blocking call, and Ash can't respond
  // to us since it runs in the same process as Chrome. After the Chrome side
  // is refactored to take a callback or Ash is split into a separate binary,
  // we can change the implementation here to query with Ash.

  IsUvpaaResponse response;

  if (!Initialized()) {
    LOG(INFO) << "IsUvpaa called but WebAuthnHandler not initialized. Maybe "
                 "U2F is on.";
    response.set_available(false);
    method_response->Return(response);
    return;
  }

  if (!auth_time_secret_hash_) {
    LOG(ERROR) << "No auth-time secret hash. MakeCredential will fail, so "
                  "reporting IsUVPAA=false.";
    response.set_available(false);
    method_response->Return(response);
    return;
  }

  base::Optional<std::string> account_id = user_state_->GetUser();
  if (!account_id) {
    LOG(ERROR) << "IsUvpaa called but no user.";
    response.set_available(false);
    method_response->Return(response);
    return;
  }

  if (HasPin(*account_id)) {
    response.set_available(true);
    method_response->Return(response);
    return;
  }

  base::Optional<std::string> sanitized_user = user_state_->GetSanitizedUser();
  DCHECK(sanitized_user);
  if (HasFingerprint(*sanitized_user)) {
    response.set_available(true);
    method_response->Return(response);
    return;
  }

  response.set_available(false);
  method_response->Return(response);
}

bool WebAuthnHandler::HasPin(const std::string& account_id) {
  cryptohome::AccountIdentifier id;
  id.set_account_id(account_id);
  cryptohome::AuthorizationRequest auth;
  cryptohome::GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label(kCryptohomePinLabel);
  cryptohome::BaseReply reply;
  brillo::ErrorPtr error;

  if (!cryptohome_proxy_->GetKeyDataEx(id, auth, req, &reply, &error,
                                       kCryptohomeTimeout.InMilliseconds())) {
    LOG(ERROR) << "Cannot query PIN availability from cryptohome, error: "
               << error->GetMessage();
    return false;
  }

  if (reply.has_error()) {
    LOG(ERROR) << "GetKeyData response has error " << reply.error();
    return false;
  }

  if (!reply.HasExtension(cryptohome::GetKeyDataReply::reply)) {
    LOG(ERROR) << "GetKeyData response doesn't have the correct extension.";
    return false;
  }

  return reply.GetExtension(cryptohome::GetKeyDataReply::reply)
             .key_data_size() > 0;
}

bool WebAuthnHandler::HasFingerprint(const std::string& sanitized_user) {
  dbus::ObjectProxy* biod_proxy = bus_->GetObjectProxy(
      biod::kBiodServiceName,
      dbus::ObjectPath(std::string(biod::kBiodServicePath)
                           .append(kCrosFpBiometricsManagerRelativePath)));

  dbus::MethodCall method_call(biod::kBiometricsManagerInterface,
                               biod::kBiometricsManagerGetRecordsForUserMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendString(sanitized_user);

  std::unique_ptr<dbus::Response> response = biod_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response) {
    LOG(ERROR)
        << "Cannot check fingerprint availability: no response from biod.";
    return false;
  }

  dbus::MessageReader response_reader(response.get());
  dbus::MessageReader records_reader(nullptr);
  if (!response_reader.PopArray(&records_reader)) {
    LOG(ERROR) << "Cannot parse GetRecordsForUser response from biod.";
    return false;
  }

  int records_count = 0;
  while (records_reader.HasMoreData()) {
    dbus::ObjectPath record_path;
    if (!records_reader.PopObjectPath(&record_path)) {
      LOG(WARNING) << "Cannot parse fingerprint record path";
      continue;
    }
    records_count++;
  }
  return records_count > 0;
}

void WebAuthnHandler::SetWebAuthnStorageForTesting(
    std::unique_ptr<WebAuthnStorage> storage) {
  webauthn_storage_ = std::move(storage);
}

void WebAuthnHandler::SetCryptohomeInterfaceProxyForTesting(
    std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
        cryptohome_proxy) {
  cryptohome_proxy_ = std::move(cryptohome_proxy);
}

}  // namespace u2f
