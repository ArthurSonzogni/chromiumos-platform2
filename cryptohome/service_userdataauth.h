// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_USERDATAAUTH_H_
#define CRYPTOHOME_SERVICE_USERDATAAUTH_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_method_response.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/signalling.h"
#include "cryptohome/userdataauth.h"
#include "dbus_adaptors/org.chromium.UserDataAuth.h"

namespace cryptohome {
class UserDataAuthAdaptor
    : public org::chromium::UserDataAuthInterfaceInterface,
      public org::chromium::UserDataAuthInterfaceAdaptor {
 public:
  explicit UserDataAuthAdaptor(scoped_refptr<dbus::Bus> bus,
                               brillo::dbus_utils::DBusObject* dbus_object,
                               UserDataAuth* service)
      : org::chromium::UserDataAuthInterfaceAdaptor(this),
        signalling_(*this),
        dbus_object_(dbus_object),
        service_(service) {
    service_->SetSignallingInterface(signalling_);
  }
  UserDataAuthAdaptor(const UserDataAuthAdaptor&) = delete;
  UserDataAuthAdaptor& operator=(const UserDataAuthAdaptor&) = delete;

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  // Note that the documentation for all of the methods below can be found in
  // either the DBus Introspection XML
  // (cryptohome/dbus_bindings/org.chromium.UserDataAuth.xml), or the protobuf
  // definition file (system_api/dbus/cryptohome/UserDataAuth.proto)
  void IsMounted(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::IsMountedReply>> response,
                 const user_data_auth::IsMountedRequest& in_request) override;
  void DoIsMounted(const Username& username,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       user_data_auth::IsMountedReply>> response);

  void GetVaultProperties(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetVaultPropertiesReply>> response,
      const user_data_auth::GetVaultPropertiesRequest& in_request) override;
  void DoGetVaultProperties(
      const user_data_auth::GetVaultPropertiesRequest& in_request,
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetVaultPropertiesReply>> response);

  void Unmount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   user_data_auth::UnmountReply>> response,
               const user_data_auth::UnmountRequest& in_request) override;
  void DoUnmount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::UnmountReply>> response);

  void Remove(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                  user_data_auth::RemoveReply>> response,
              const user_data_auth::RemoveRequest& in_request) override;
  void DoRemove(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    user_data_auth::RemoveReply>> response,
                const user_data_auth::RemoveRequest& in_request);

  void GetWebAuthnSecret(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetWebAuthnSecretReply>> response,
      const user_data_auth::GetWebAuthnSecretRequest& in_request) override;

  void DoGetWebAuthnSecret(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetWebAuthnSecretReply>> response,
      const user_data_auth::GetWebAuthnSecretRequest& in_request);

  void GetWebAuthnSecretHash(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetWebAuthnSecretHashReply>> response,
      const user_data_auth::GetWebAuthnSecretHashRequest& in_request) override;

  void DoGetWebAuthnSecretHash(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetWebAuthnSecretHashReply>> response,
      const user_data_auth::GetWebAuthnSecretHashRequest& in_request);

  void GetRecoverableKeyStores(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetRecoverableKeyStoresReply>> response,
      const user_data_auth::GetRecoverableKeyStoresRequest& in_request)
      override;

  void DoGetRecoverableKeyStores(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetRecoverableKeyStoresReply>> response,
      const user_data_auth::GetRecoverableKeyStoresRequest& in_request);

  void StartMigrateToDircrypto(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::StartMigrateToDircryptoReply>> response,
      const user_data_auth::StartMigrateToDircryptoRequest& in_request)
      override;

  void DoStartMigrateToDircrypto(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::StartMigrateToDircryptoReply>> response,
      const user_data_auth::StartMigrateToDircryptoRequest& in_request);

  void NeedsDircryptoMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::NeedsDircryptoMigrationReply>> response,
      const user_data_auth::NeedsDircryptoMigrationRequest& in_request)
      override;
  void DoNeedsDircryptoMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::NeedsDircryptoMigrationReply>> response,
      const user_data_auth::NeedsDircryptoMigrationRequest& in_request);

  void GetSupportedKeyPolicies(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSupportedKeyPoliciesReply>> response,
      const user_data_auth::GetSupportedKeyPoliciesRequest& in_request)
      override;

  void GetAccountDiskUsage(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAccountDiskUsageReply>> response,
      const user_data_auth::GetAccountDiskUsageRequest& in_request) override;
  void DoGetAccountDiskUsage(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAccountDiskUsageReply>> response,
      const user_data_auth::GetAccountDiskUsageRequest& in_request);

  void StartAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::StartAuthSessionReply>> response,
      const user_data_auth::StartAuthSessionRequest& in_request) override;

  void DoStartAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::StartAuthSessionReply>> response,
      const user_data_auth::StartAuthSessionRequest& in_request);

  void InvalidateAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InvalidateAuthSessionReply>> response,
      const user_data_auth::InvalidateAuthSessionRequest& in_request) override;

  void DoInvalidateAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InvalidateAuthSessionReply>> response,
      const user_data_auth::InvalidateAuthSessionRequest& in_request);

  void ExtendAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ExtendAuthSessionReply>> response,
      const user_data_auth::ExtendAuthSessionRequest& in_request) override;

  void DoExtendAuthSession(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ExtendAuthSessionReply>> response,
      const user_data_auth::ExtendAuthSessionRequest& in_request);

  void CreatePersistentUser(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::CreatePersistentUserReply>> response,
      const user_data_auth::CreatePersistentUserRequest& in_request) override;

  void DoCreatePersistentUser(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::CreatePersistentUserReply>> response,
      const user_data_auth::CreatePersistentUserRequest& in_request);

  void PrepareGuestVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareGuestVaultReply>> response,
      const user_data_auth::PrepareGuestVaultRequest& in_request) override;

  void DoPrepareGuestVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareGuestVaultReply>> response,
      const user_data_auth::PrepareGuestVaultRequest& in_request);

  void PrepareEphemeralVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareEphemeralVaultReply>> response,
      const user_data_auth::PrepareEphemeralVaultRequest& in_request) override;

  void DoPrepareEphemeralVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareEphemeralVaultReply>> response,
      const user_data_auth::PrepareEphemeralVaultRequest& in_request);

  void PreparePersistentVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PreparePersistentVaultReply>> response,
      const user_data_auth::PreparePersistentVaultRequest& in_request) override;

  void DoPreparePersistentVault(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PreparePersistentVaultReply>> response,
      const user_data_auth::PreparePersistentVaultRequest& in_request);

  void PrepareVaultForMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareVaultForMigrationReply>> response,
      const user_data_auth::PrepareVaultForMigrationRequest& in_request)
      override;

  void DoPrepareVaultForMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareVaultForMigrationReply>> response,
      const user_data_auth::PrepareVaultForMigrationRequest& in_request);

  void AddAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::AddAuthFactorReply>> response,
      const user_data_auth::AddAuthFactorRequest& in_request) override;

  void DoAddAuthFactor(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                           user_data_auth::AddAuthFactorReply>> response,
                       const user_data_auth::AddAuthFactorRequest& in_request);

  void UpdateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateAuthFactorReply>> response,
      const user_data_auth::UpdateAuthFactorRequest& in_request) override;

  void DoUpdateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateAuthFactorReply>> response,
      const user_data_auth::UpdateAuthFactorRequest& in_request);

  void UpdateAuthFactorMetadata(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateAuthFactorMetadataReply>> response,
      const user_data_auth::UpdateAuthFactorMetadataRequest& in_request)
      override;

  void DoUpdateAuthFactorMetadata(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateAuthFactorMetadataReply>> response,
      const user_data_auth::UpdateAuthFactorMetadataRequest& in_request);

  void RelabelAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::RelabelAuthFactorReply>> response,
      const user_data_auth::RelabelAuthFactorRequest& in_request) override;

  void DoRelabelAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::RelabelAuthFactorReply>> response,
      const user_data_auth::RelabelAuthFactorRequest& in_request);

  void ReplaceAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ReplaceAuthFactorReply>> response,
      const user_data_auth::ReplaceAuthFactorRequest& in_request) override;

  void DoReplaceAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ReplaceAuthFactorReply>> response,
      const user_data_auth::ReplaceAuthFactorRequest& in_request);

  void RemoveAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::RemoveAuthFactorReply>> response,
      const user_data_auth::RemoveAuthFactorRequest& in_request) override;

  void DoRemoveAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::RemoveAuthFactorReply>> response,
      const user_data_auth::RemoveAuthFactorRequest& in_request);

  void ListAuthFactors(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ListAuthFactorsReply>> response,
      const user_data_auth::ListAuthFactorsRequest& in_request) override;

  void DoListAuthFactors(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ListAuthFactorsReply>> response,
      const user_data_auth::ListAuthFactorsRequest& in_request);

  void GetAuthFactorExtendedInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAuthFactorExtendedInfoReply>> response,
      const user_data_auth::GetAuthFactorExtendedInfoRequest& in_request)
      override;

  void DoGetAuthFactorExtendedInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAuthFactorExtendedInfoReply>> response,
      const user_data_auth::GetAuthFactorExtendedInfoRequest& in_request);

  void GenerateFreshRecoveryId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GenerateFreshRecoveryIdReply>> response,
      const user_data_auth::GenerateFreshRecoveryIdRequest& in_request)
      override;

  void DoGenerateFreshRecoveryId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GenerateFreshRecoveryIdReply>> response,
      const user_data_auth::GenerateFreshRecoveryIdRequest& in_request);

  void PrepareAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareAuthFactorReply>> response,
      const user_data_auth::PrepareAuthFactorRequest& in_request) override;

  void DoPrepareAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::PrepareAuthFactorReply>> response,
      const user_data_auth::PrepareAuthFactorRequest& in_request);

  void TerminateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::TerminateAuthFactorReply>> response,
      const user_data_auth::TerminateAuthFactorRequest& in_request) override;

  void DoTerminateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::TerminateAuthFactorReply>> response,
      const user_data_auth::TerminateAuthFactorRequest& in_request);

  void AuthenticateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::AuthenticateAuthFactorReply>> response,
      const user_data_auth::AuthenticateAuthFactorRequest& in_request) override;

  void DoAuthenticateAuthFactor(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::AuthenticateAuthFactorReply>> response,
      const user_data_auth::AuthenticateAuthFactorRequest& in_request);

  void GetAuthSessionStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAuthSessionStatusReply>> response,
      const user_data_auth::GetAuthSessionStatusRequest& in_request) override;

  void DoGetAuthSessionStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAuthSessionStatusReply>> response,
      const user_data_auth::GetAuthSessionStatusRequest& in_request);

  void LockFactorUntilReboot(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::LockFactorUntilRebootReply>> response,
      const user_data_auth::LockFactorUntilRebootRequest& in_request) override;

  void DoLockFactorUntilReboot(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::LockFactorUntilRebootReply>> response,
      const user_data_auth::LockFactorUntilRebootRequest& in_request);

  void CreateVaultKeyset(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::CreateVaultKeysetReply>> response,
      const user_data_auth::CreateVaultKeysetRequest& in_request) override;

  void DoCreateVaultKeyset(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::CreateVaultKeysetReply>> response,
      const user_data_auth::CreateVaultKeysetRequest& in_request);

  void ResetApplicationContainer(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ResetApplicationContainerReply>> response,
      const user_data_auth::ResetApplicationContainerRequest& in_request)
      override;
  void DoResetApplicationContainer(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::ResetApplicationContainerReply>> response,
      const user_data_auth::ResetApplicationContainerRequest& in_request);

  void MigrateLegacyFingerprints(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::MigrateLegacyFingerprintsReply>> response,
      const user_data_auth::MigrateLegacyFingerprintsRequest& in_request)
      override;
  void DoMigrateLegacyFingerprints(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::MigrateLegacyFingerprintsReply>> response,
      const user_data_auth::MigrateLegacyFingerprintsRequest& in_request);

  void GetArcDiskFeatures(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetArcDiskFeaturesReply>> response,
      const user_data_auth::GetArcDiskFeaturesRequest& in_request) override;

  void SetUserDataStorageWriteEnabled(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::SetUserDataStorageWriteEnabledReply>> response,
      const user_data_auth::SetUserDataStorageWriteEnabledRequest& in_request)
      override;
  void DoSetUserDataStorageWriteEnabled(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::SetUserDataStorageWriteEnabledReply>> response,
      const user_data_auth::SetUserDataStorageWriteEnabledRequest& in_request);

 private:
  // Implements the signalling interface for this service. All of the send
  // operations are implemented by forwarding to the relevant adaptor function.
  class Signalling : public SignallingInterface {
   public:
    explicit Signalling(UserDataAuthInterfaceAdaptor& adaptor)
        : adaptor_(&adaptor) {}

    Signalling(const Signalling&) = delete;
    Signalling& operator=(const Signalling&) = delete;

   private:
    void SendAuthFactorStatusUpdate(
        const user_data_auth::AuthFactorStatusUpdate& signal) override {
      adaptor_->SendAuthFactorStatusUpdateSignal(signal);
    }
    void SendLowDiskSpace(const user_data_auth::LowDiskSpace& signal) override {
      adaptor_->SendLowDiskSpaceSignal(signal);
    }
    void SendPrepareAuthFactorProgress(
        const user_data_auth::PrepareAuthFactorProgress& signal) override {
      adaptor_->SendPrepareAuthFactorProgressSignal(signal);
    }
    void SendAuthenticateStarted(
        const user_data_auth::AuthenticateStarted& signal) override {
      adaptor_->SendAuthenticateStartedSignal(signal);
    }
    void SendAuthenticateAuthFactorCompleted(
        const user_data_auth::AuthenticateAuthFactorCompleted& signal)
        override {
      adaptor_->SendAuthenticateAuthFactorCompletedSignal(signal);
    }
    void SendMountStarted(const user_data_auth::MountStarted& signal) override {
      adaptor_->SendMountStartedSignal(signal);
    }
    void SendMountCompleted(
        const user_data_auth::MountCompleted& signal) override {
      adaptor_->SendMountCompletedSignal(signal);
    }
    void SendAuthFactorAdded(
        const user_data_auth::AuthFactorAdded& signal) override {
      adaptor_->SendAuthFactorAddedSignal(signal);
    }
    void SendAuthFactorRemoved(
        const user_data_auth::AuthFactorRemoved& signal) override {
      adaptor_->SendAuthFactorRemovedSignal(signal);
    }
    void SendAuthFactorUpdated(
        const user_data_auth::AuthFactorUpdated& signal) override {
      adaptor_->SendAuthFactorUpdatedSignal(signal);
    }
    void SendAuthSessionExpiring(
        const user_data_auth::AuthSessionExpiring& signal) override {
      adaptor_->SendAuthSessionExpiringSignal(signal);
    }
    void SendRemoveCompleted(
        const user_data_auth::RemoveCompleted& signal) override {
      adaptor_->SendRemoveCompletedSignal(signal);
    }

    UserDataAuthInterfaceAdaptor* adaptor_;
  };
  Signalling signalling_;

  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  // Factory used to construct weak pointers when posting tasks to the mount
  // thread. The pointers must not be used for tasks on other threads.
  base::WeakPtrFactory<UserDataAuthAdaptor> weak_factory_{this};
};

class Pkcs11Adaptor : public org::chromium::CryptohomePkcs11InterfaceInterface,
                      public org::chromium::CryptohomePkcs11InterfaceAdaptor {
 public:
  explicit Pkcs11Adaptor(scoped_refptr<dbus::Bus> bus,
                         brillo::dbus_utils::DBusObject* dbus_object,
                         UserDataAuth* service)
      : org::chromium::CryptohomePkcs11InterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }
  Pkcs11Adaptor(const Pkcs11Adaptor&) = delete;
  Pkcs11Adaptor& operator=(const Pkcs11Adaptor&) = delete;

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  // Note that the documentation for all of the methods below can be found in
  // either the DBus Introspection XML
  // (cryptohome/dbus_bindings/org.chromium.UserDataAuth.xml), or the protobuf
  // definition file (system_api/dbus/cryptohome/UserDataAuth.proto)
  void Pkcs11IsTpmTokenReady(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
      const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) override;
  void DoPkcs11IsTpmTokenReady(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
      const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request);

  void Pkcs11GetTpmTokenInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11GetTpmTokenInfoReply>> response,
      const user_data_auth::Pkcs11GetTpmTokenInfoRequest& in_request) override;

  void Pkcs11Terminate(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11TerminateReply>> response,
      const user_data_auth::Pkcs11TerminateRequest& in_request) override;
  void DoPkcs11Terminate(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11TerminateReply>> response,
      const user_data_auth::Pkcs11TerminateRequest& in_request);

  void Pkcs11RestoreTpmTokens(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11RestoreTpmTokensReply>> response,
      const user_data_auth::Pkcs11RestoreTpmTokensRequest& in_request) override;
  void DoPkcs11RestoreTpmTokens(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11RestoreTpmTokensReply>> response,
      const user_data_auth::Pkcs11RestoreTpmTokensRequest& in_request);

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  // Factory used to construct weak pointers when posting tasks to the mount
  // thread. The pointers must not be used for tasks on other threads.
  base::WeakPtrFactory<Pkcs11Adaptor> weak_factory_{this};
};

class CryptohomeMiscAdaptor
    : public org::chromium::CryptohomeMiscInterfaceInterface,
      public org::chromium::CryptohomeMiscInterfaceAdaptor {
 public:
  explicit CryptohomeMiscAdaptor(scoped_refptr<dbus::Bus> bus,
                                 brillo::dbus_utils::DBusObject* dbus_object,
                                 UserDataAuth* service)
      : org::chromium::CryptohomeMiscInterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }
  CryptohomeMiscAdaptor(const CryptohomeMiscAdaptor&) = delete;
  CryptohomeMiscAdaptor& operator=(const CryptohomeMiscAdaptor&) = delete;

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  void GetSystemSalt(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSystemSaltReply>> response,
      const user_data_auth::GetSystemSaltRequest& in_request) override;

  void UpdateCurrentUserActivityTimestamp(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
      const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
          in_request) override;
  void DoUpdateCurrentUserActivityTimestamp(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
      const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
          in_request);

  void GetSanitizedUsername(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSanitizedUsernameReply>> response,
      const user_data_auth::GetSanitizedUsernameRequest& in_request) override;
  void GetLoginStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetLoginStatusReply>> response,
      const user_data_auth::GetLoginStatusRequest& in_request) override;

  void LockToSingleUserMountUntilReboot(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::LockToSingleUserMountUntilRebootReply>> response,
      const user_data_auth::LockToSingleUserMountUntilRebootRequest& in_request)
      override;
  void GetRsuDeviceId(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetRsuDeviceIdReply>> response,
      const user_data_auth::GetRsuDeviceIdRequest& in_request) override;

  void GetPinWeaverInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetPinWeaverInfoReply>> response,
      const user_data_auth::GetPinWeaverInfoRequest& in_request) override;
  void DoGetPinWeaverInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetPinWeaverInfoReply>> response,
      const user_data_auth::GetPinWeaverInfoRequest& in_request);

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  // Factory used to construct weak pointers when posting tasks to the mount
  // thread. The pointers must not be used for tasks on other threads.
  base::WeakPtrFactory<CryptohomeMiscAdaptor> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_USERDATAAUTH_H_
