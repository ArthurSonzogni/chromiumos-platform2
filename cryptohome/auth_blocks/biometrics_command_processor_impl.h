// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_IMPL_H_
#define CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_IMPL_H_

#include "cryptohome/auth_blocks/biometrics_command_processor.h"

#include <memory>
#include <optional>
#include <string>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <biod/biod_proxy/auth_stack_manager_proxy_base.h>
#include <biod/proto_bindings/messages.pb.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>

namespace cryptohome {

class BiometricsCommandProcessorImpl : public BiometricsCommandProcessor {
 public:
  explicit BiometricsCommandProcessorImpl(
      std::unique_ptr<biod::AuthStackManagerProxyBase> proxy);
  BiometricsCommandProcessorImpl(const BiometricsCommandProcessorImpl&) =
      delete;
  BiometricsCommandProcessorImpl& operator=(
      const BiometricsCommandProcessorImpl&) = delete;

  // BiometricsCommandProcessor methods.
  bool IsReady() override;
  void SetEnrollScanDoneCallback(
      base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
          on_done) override;
  void SetAuthScanDoneCallback(
      base::RepeatingCallback<void(user_data_auth::AuthScanDone)> on_done)
      override;
  void SetSessionFailedCallback(
      base::RepeatingCallback<void()> on_failure) override;
  void GetNonce(
      base::OnceCallback<void(std::optional<brillo::Blob>)> callback) override;
  void StartEnrollSession(OperationInput payload,
                          base::OnceCallback<void(bool)> on_done) override;
  void EnrollLegacyTemplate(const std::string& legacy_record_id,
                            OperationInput payload,
                            base::OnceCallback<void(bool)> on_done) override;
  void StartAuthenticateSession(
      ObfuscatedUsername obfuscated_username,
      OperationInput payload,
      base::OnceCallback<void(bool)> on_done) override;
  void CreateCredential(OperationCallback on_done) override;
  void MatchCredential(OperationCallback on_done) override;
  void EndEnrollSession() override;
  void EndAuthenticateSession() override;
  void DeleteCredential(
      ObfuscatedUsername obfuscated_username,
      const std::string& record_id,
      base::OnceCallback<void(DeleteResult)> on_done) override;
  void ListLegacyRecords(LegacyRecordsCallback on_done) override;

 private:
  // If signal is successfully connected, decrement the
  // |pending_signal_connections_| counter.
  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool success);
  // This is used as the signal callback we register to the biod proxy. It
  // parses the signal into an AuthEnrollmentProgress proto and triggers
  // on_enroll_scan_done_.
  void OnEnrollScanDone(dbus::Signal* signal);
  // This is used as the signal callback we register to the biod proxy. It
  // parses the signal into an AuthScanDone proto and triggers
  // on_auth_scan_done_.
  void OnAuthScanDone(dbus::Signal* signal);
  // This is used as the OnSessionFailed callback we register to the biod proxy.
  void OnSessionFailed(dbus::Signal* signal);
  // This is used as the callback of biod proxy's CreateCredential method. It
  // decrypts the secret data contained in the response with the session key and
  // packs it into OperationOutput.
  void OnCreateCredentialReply(
      OperationCallback on_done,
      crypto::ScopedEC_KEY key,
      std::optional<biod::CreateCredentialReply> reply);
  // This is used as the callback of biod proxy's AuthenticateCredential method.
  // It decrypts the secret data contained in the response with the session key
  // and packs it into OperationOutput.
  void OnAuthenticateCredentialReply(
      OperationCallback on_done,
      crypto::ScopedEC_KEY key,
      std::optional<biod::AuthenticateCredentialReply> reply);
  void OnDeleteCredentialReply(
      base::OnceCallback<void(DeleteResult)> on_done,
      std::optional<biod::DeleteCredentialReply> reply);
  void OnListLegacyRecordsReply(
      LegacyRecordsCallback on_done,
      std::optional<biod::ListLegacyRecordsReply> reply);

  base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
      on_enroll_scan_done_;
  base::RepeatingCallback<void(user_data_auth::AuthScanDone)>
      on_auth_scan_done_;
  base::RepeatingCallback<void()> on_session_failed_;
  std::unique_ptr<biod::AuthStackManagerProxyBase> proxy_;
  // This will be initialized to the total signals this class subsribes to.
  // Whenever a signal is connected successfully, the count will be decremented.
  // When the count is zero, this command processor is in ready state for
  // commands.
  int pending_signal_connections_;

  // The last member, to invalidate weak references first on destruction.
  base::WeakPtrFactory<BiometricsCommandProcessorImpl> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_IMPL_H_
