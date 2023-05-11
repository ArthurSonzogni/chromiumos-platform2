// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/keymint_server.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <keymaster/android_keymaster_messages.h>
#include <mojo/keymint.mojom.h>

// The implementations of |arc::mojom::KeyMintServer| methods below have the
// following overall pattern:
//
// * Generate an std::unique_ptr to a KeyMint request data structure from the
//   arguments received from Mojo, usually through the helpers in conversion.h.
//
// * Execute the operation in |backend->keymint()|, posting this task to a
//   background thread. This produces a KeyMint response data structure.
//
// * Post the response to a callback that runs on the original thread (in this
//   case, the Mojo thread where the request started).
//
// * Convert the KeyMint response to the Mojo return values, and run the
//   result callback.
//
namespace arc::keymint {

KeyMintServer::Backend::Backend() = default;

KeyMintServer::Backend::~Backend() = default;

KeyMintServer::KeyMintServer()
    : backend_thread_("BackendKeyMintThread"), weak_ptr_factory_(this) {
  CHECK(backend_thread_.Start()) << "Failed to start keymint thread";
}

KeyMintServer::~KeyMintServer() = default;

void KeyMintServer::UpdateContextPlaceholderKeys(
    std::vector<mojom::ChromeOsKeyPtr> keys,
    base::OnceCallback<void(bool)> callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::SetSystemVersion(uint32_t android_version,
                                     uint32_t android_patchlevel) {
  // TODO(b/274723521): Add this back.
}

template <typename KmMember, typename KmRequest, typename KmResponse>
void KeyMintServer::RunKeyMintRequest(
    const base::Location& location,
    KmMember member,
    std::unique_ptr<KmRequest> request,
    base::OnceCallback<void(std::unique_ptr<KmResponse>)> callback) {
  // TODO(b/274723521): Add this back.
}

void KeyMintServer::AddRngEntropy(const std::vector<uint8_t>& data,
                                  AddRngEntropyCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::GetKeyCharacteristics(
    arc::mojom::keymint::GetKeyCharacteristicsRequestPtr request,
    GetKeyCharacteristicsCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::GenerateKey(
    arc::mojom::keymint::GenerateKeyRequestPtr request,
    GenerateKeyCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::ImportKey(arc::mojom::keymint::ImportKeyRequestPtr request,
                              ImportKeyCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::ImportWrappedKey(
    arc::mojom::keymint::ImportWrappedKeyRequestPtr request,
    ImportWrappedKeyCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::UpgradeKey(
    arc::mojom::keymint::UpgradeKeyRequestPtr request,
    UpgradeKeyCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::DeleteKey(const std::vector<uint8_t>& key_blob,
                              DeleteKeyCallback callback) {
  // TODO(b/274723521): Add this back.
}

void KeyMintServer::DeleteAllKeys(DeleteAllKeysCallback callback) {
  // TODO(b/274723521): Add this back.
}

void KeyMintServer::DestroyAttestationIds(
    DestroyAttestationIdsCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::Begin(arc::mojom::keymint::BeginRequestPtr request,
                          BeginCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::DeviceLocked(
    bool password_only,
    arc::mojom::keymint::TimeStampTokenPtr timestamp_token,
    DeviceLockedCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::EarlyBootEnded(EarlyBootEndedCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::ConvertStorageKeyToEphemeral(
    const std::vector<uint8_t>& storage_key_blob,
    ConvertStorageKeyToEphemeralCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::GetRootOfTrustChallenge(
    GetRootOfTrustChallengeCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::GetRootOfTrust(const std::vector<uint8_t>& challenge,
                                   GetRootOfTrustCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::SendRootOfTrust(const std::vector<uint8_t>& root_of_trust,
                                    SendRootOfTrustCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::UpdateAad(arc::mojom::keymint::UpdateAadRequestPtr request,
                              UpdateAadCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::Update(arc::mojom::keymint::UpdateRequestPtr request,
                           UpdateCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::Finish(arc::mojom::keymint::FinishRequestPtr request,
                           FinishCallback callback) {
  // TODO(b/274723521): Finish this.
}

void KeyMintServer::Abort(uint64_t op_handle, AbortCallback callback) {
  // TODO(b/274723521): Finish this.
}

}  // namespace arc::keymint
