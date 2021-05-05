// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_WEBAUTHN_HANDLER_H_
#define U2FD_WEBAUTHN_HANDLER_H_

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome-client/cryptohome/dbus-proxies.h>
#include <metrics/metrics_library.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/allowlisting_util.h"
#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/u2f_mode.h"
#include "u2fd/user_state.h"
#include "u2fd/webauthn_storage.h"

namespace u2f {

using MakeCredentialMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<MakeCredentialResponse>;
using GetAssertionMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<GetAssertionResponse>;
using IsUvpaaMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<IsUvpaaResponse>;
using ::google::protobuf::RepeatedPtrField;

struct MakeCredentialSession {
  bool empty() { return !response; }
  uint64_t session_id;
  MakeCredentialRequest request;
  std::unique_ptr<MakeCredentialMethodResponse> response;
  bool canceled = false;
};

struct GetAssertionSession {
  bool empty() { return !response; }
  uint64_t session_id;
  GetAssertionRequest request;
  // The credential_id to send to the TPM. May be a resident credential.
  std::string credential_id;
  std::unique_ptr<GetAssertionMethodResponse> response;
  bool canceled = false;
};

struct MatchedCredentials {
  std::vector<std::string> platform_credentials;
  std::vector<std::string> legacy_credentials_for_rp_id;
  std::vector<std::string> legacy_credentials_for_app_id;
  bool has_internal_error = false;
};

enum class PresenceRequirement {
  kNone,  // Does not require presence. Used only after user-verification in
          // MakeCredential.
  kPowerButton,  // Requires a power button press as indication of presence.
  kFingerprint,  // Requires the GPIO line from fingerprint MCU to be active.
  kAuthorizationSecret,  // Requires the correct authorization secret.
};

// Implementation of the WebAuthn DBus API.
// More detailed documentation is available in u2f_interface.proto
class WebAuthnHandler {
 public:
  WebAuthnHandler();

  // Initializes WebAuthnHandler.
  // |bus| - DBus pointer.
  // |tpm_proxy| - proxy to send commands to TPM. Owned by U2fDaemon and should
  // outlive WebAuthnHandler.
  // |user_state| - pointer to a UserState instance, for requesting user secret.
  // Owned by U2fDaemon and should outlive WebAuthnHandler.
  // |u2f_mode| - whether u2f or g2f is enabled.
  // |request_presence| - callback for performing other platform tasks when
  // expecting the user to press the power button.
  // |allowlisting_util| - utility to append allowlisting data to g2f certs.
  // |metrics| pointer to metrics library object.
  void Initialize(dbus::Bus* bus,
                  TpmVendorCommandProxy* tpm_proxy,
                  UserState* user_state,
                  U2fMode u2f_mode,
                  std::function<void()> request_presence,
                  std::unique_ptr<AllowlistingUtil> allowlisting_util,
                  MetricsLibraryInterface* metrics);

  // Called when session state changed. Loads/clears state for primary user.
  void OnSessionStarted(const std::string& account_id);
  void OnSessionStopped();

  // Generates a new credential.
  void MakeCredential(
      std::unique_ptr<MakeCredentialMethodResponse> method_response,
      const MakeCredentialRequest& request);

  // Signs a challenge from the relaying party.
  void GetAssertion(std::unique_ptr<GetAssertionMethodResponse> method_response,
                    const GetAssertionRequest& request);

  // Tests validity and/or presence of specified credentials, including u2fhid
  // credentials.
  HasCredentialsResponse HasCredentials(const HasCredentialsRequest& request);

  // Tests whether any credential were registered using the u2fhid (on either
  // WebAuthn API or U2F API).
  HasCredentialsResponse HasLegacyCredentials(
      const HasCredentialsRequest& request);

  // Dismisses user verification UI and abort the operation. This is expected to
  // be called by the browser only in UV operations, because UP operations
  // themselves will timeout after ~5 seconds.
  CancelWebAuthnFlowResponse Cancel(const CancelWebAuthnFlowRequest& request);

  // Checks whether user-verifying platform authenticator is available.
  void IsUvpaa(std::unique_ptr<IsUvpaaMethodResponse> method_response,
               const IsUvpaaRequest& request);

  // Checks whether u2f is enabled (therefore power button mode is supported).
  IsU2fEnabledResponse IsU2fEnabled(const IsU2fEnabledRequest& request);

  void SetWebAuthnStorageForTesting(std::unique_ptr<WebAuthnStorage> storage);

  void SetCryptohomeInterfaceProxyForTesting(
      std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
          cryptohome_proxy);

 private:
  friend class WebAuthnHandlerTestBase;
  friend class WebAuthnHandlerTestAllowUP;

  bool Initialized();

  // Fetches auth-time WebAuthn secret and keep the hash of it.
  void GetWebAuthnSecretAsync(const std::string& account_id);
  void OnGetWebAuthnSecretResp(const cryptohome::BaseReply& reply);
  void OnGetWebAuthnSecretCallFailed(brillo::Error* error);

  // Callbacks invoked when UI completes user verification flow.
  void HandleUVFlowResultMakeCredential(dbus::Response* flow_response);
  void HandleUVFlowResultGetAssertion(dbus::Response* flow_response);

  // Proceeds to cr50 for the current MakeCredential request, and responds to
  // the request with authenticator data.
  // Called directly if the request is user-presence only.
  // Called on user verification success if the request is user-verification.
  void DoMakeCredential(struct MakeCredentialSession session,
                        PresenceRequirement presence_requirement);

  // Find all matching credentials and return them in 3 categories (see struct
  // MatchedCredentials definition). If a legacy credential matches both rp_id
  // and app_id, it will only appear in "legacy_credentials_for_rp_id".
  MatchedCredentials FindMatchedCredentials(
      const RepeatedPtrField<std::string>& all_credentials,
      const std::string& rp_id,
      const std::string& app_id);

  // Inserts the hash of auth-time secret into a versioned KH to form a
  // WebAuthn credential id.
  void InsertAuthTimeSecretHashToCredentialId(std::vector<uint8_t>* input);

  // Removes the hash of auth-time secret into a credential id so that cr50
  // receives the original versioned KH.
  void RemoveAuthTimeSecretHashFromCredentialId(std::vector<uint8_t>* input);

  // Proceeds to cr50 for the current GetAssertion request, and responds to
  // the request with assertions.
  // Called directly if the request is user-presence only.
  // Called on user verification success if the request is user-verification.
  void DoGetAssertion(struct GetAssertionSession session,
                      PresenceRequirement presence_requirement);

  // Runs a U2F_GENERATE command to create a new key handle, and stores the key
  // handle in |credential_id| and the public key in |credential_public_key|.
  // The flag in the U2F_GENERATE command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  MakeCredentialResponse::MakeCredentialStatus DoU2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Repeatedly sends u2f_generate request to the TPM if there's no presence.
  template <typename Response>
  MakeCredentialResponse::MakeCredentialStatus SendU2fGenerateWaitForPresence(
      struct u2f_generate_req* generate_req,
      Response* generate_resp,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Runs a U2F_SIGN command to check that credential_id is valid, and if so,
  // sign |hash_to_sign| and store the signature in |signature|.
  // The flag in the U2F_SIGN command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  GetAssertionResponse::GetAssertionStatus DoU2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature);

  // Repeatedly sends u2f_sign request to the TPM if there's no presence.
  template <typename Request>
  GetAssertionResponse::GetAssertionStatus SendU2fSignWaitForPresence(
      Request* sign_req,
      struct u2f_sign_resp* sign_resp,
      std::vector<uint8_t>* signature);

  // Runs a U2F_SIGN command with "check only" flag to check whether
  // |credential_id| is a key handle owned by this device tied to |rp_id_hash|.
  HasCredentialsResponse::HasCredentialsStatus DoU2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret);

  // Run a U2F_ATTEST command to sign data using the cr50 individual attestation
  // certificate.
  MakeCredentialResponse::MakeCredentialStatus DoG2fAttest(
      const std::vector<uint8_t>& data,
      uint8_t format,
      std::vector<uint8_t>* signature_out);

  // Prompts the user for presence through |request_presence_| and calls |fn|
  // repeatedly until success or timeout.
  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status);

  // Creates and returns authenticator data. |include_attested_credential_data|
  // should be set to true for MakeCredential, false for GetAssertion.
  base::Optional<std::vector<uint8_t>> MakeAuthenticatorData(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_public_key,
      bool user_verified,
      bool include_attested_credential_data,
      bool is_u2f_authenticator_credential);

  // Appends a none attestation to |response|. Only used in MakeCredential.
  void AppendNoneAttestation(MakeCredentialResponse* response);

  // Creates and returns an U2F attestation statement for |data_to_sign|, or
  // nullopt if attestation fails.
  base::Optional<std::vector<uint8_t>> MakeFidoU2fAttestationStatement(
      const std::vector<uint8_t>& data_to_sign,
      const MakeCredentialRequest::AttestationConveyancePreference
          attestation_conveyance_preference);

  // Runs U2F_SIGN command with "check only" flag on each excluded credential
  // id. Returns true if one of them belongs to this device.
  HasCredentialsResponse::HasCredentialsStatus HasExcludedCredentials(
      const MakeCredentialRequest& request);

  // Checks whether the user with |account_id| has PIN set up.
  bool HasPin(const std::string& account_id);

  // Checks whether the user with |account_id| has fingerprint set up.
  bool HasFingerprint(const std::string& account_id);

  // Returns whether presence-only mode (power button mode) is allowed.
  bool AllowPresenceMode();

  TpmVendorCommandProxy* tpm_proxy_ = nullptr;
  UserState* user_state_ = nullptr;
  std::function<void()> request_presence_;
  dbus::Bus* bus_ = nullptr;
  // Proxy to user authentication dialog in Ash. Used only in UV requests.
  dbus::ObjectProxy* auth_dialog_dbus_proxy_ = nullptr;

  std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
      cryptohome_proxy_;

  // Presence-only mode (power button mode) should only be allowed if u2f or
  // g2f is enabled for the device (it's a per-device policy). The mode also
  // determines the attestation to add to MakeCredential.
  U2fMode u2f_mode_;

  // Util to append allowlisting data to g2f certificates.
  std::unique_ptr<AllowlistingUtil> allowlisting_util_;

  // The MakeCredential session that's waiting on UI. There can only be one
  // such session. UP sessions should not use this since there can be multiple.
  base::Optional<MakeCredentialSession> pending_uv_make_credential_session_;

  // The GetAssertion session that's waiting on UI. There can only be one
  // such session. UP sessions should not use this since there can be multiple.
  base::Optional<GetAssertionSession> pending_uv_get_assertion_session_;

  // Hash of the per-user auth-time secret for WebAuthn.
  std::unique_ptr<brillo::Blob> auth_time_secret_hash_;

  // Storage for WebAuthn credential records.
  std::unique_ptr<WebAuthnStorage> webauthn_storage_;

  MetricsLibraryInterface* metrics_;
};

}  // namespace u2f

#endif  // U2FD_WEBAUTHN_HANDLER_H_
