// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <chromeos/libhwsec/task_dispatching_framework.h>

#include "cryptohome/service_userdataauth.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

using ::hwsec::ThreadSafeDBusMethodResponse;

void UserDataAuthAdaptor::IsMounted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::IsMountedReply>> response,
    const user_data_auth::IsMountedRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoIsMounted, base::Unretained(this),
          in_request.username(),
          ThreadSafeDBusMethodResponse<user_data_auth::IsMountedReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoIsMounted(
    const std::string username,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<user_data_auth::IsMountedReply>>
        response) {
  bool is_ephemeral = false;
  bool is_mounted = service_->IsMounted(username, &is_ephemeral);

  user_data_auth::IsMountedReply reply;
  reply.set_is_mounted(is_mounted);
  reply.set_is_ephemeral_mount(is_ephemeral);
  std::move(response)->Return(reply);
}

void UserDataAuthAdaptor::Unmount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UnmountReply>> response,
    const user_data_auth::UnmountRequest& in_request) {
  // Unmount request doesn't have any parameters
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoUnmount, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::UnmountReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoUnmount(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<user_data_auth::UnmountReply>>
        response) {
  bool unmount_ok = service_->Unmount();

  user_data_auth::UnmountReply reply;
  if (!unmount_ok) {
    reply.set_error(
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::Mount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MountReply>> response,
    const user_data_auth::MountRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoMount, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::MountReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoMount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MountReply>> response,
    const user_data_auth::MountRequest& in_request) {
  service_->DoMount(
      in_request, base::BindOnce(
                      [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             user_data_auth::MountReply>> local_response,
                         const user_data_auth::MountReply& reply) {
                        local_response->Return(reply);
                      },
                      std::move(response)));
}

void UserDataAuthAdaptor::Remove(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveReply>> response,
    const user_data_auth::RemoveRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRemove, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::RemoveReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemove(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveReply>> response,
    const user_data_auth::RemoveRequest& in_request) {
  user_data_auth::RemoveReply reply;
  auto status = service_->Remove(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::Rename(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RenameReply>> response,
    const user_data_auth::RenameRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRename, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::RenameReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRename(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RenameReply>> response,
    const user_data_auth::RenameRequest& in_request) {
  user_data_auth::RenameReply reply;
  auto status = service_->Rename(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::ListKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListKeysReply>> response,
    const user_data_auth::ListKeysRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoListKeys, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::ListKeysReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoListKeys(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListKeysReply>> response,
    const user_data_auth::ListKeysRequest& in_request) {
  // TODO(b/136152258): Add unit test for this method.
  user_data_auth::ListKeysReply reply;
  std::vector<std::string> labels;
  auto status = service_->ListKeys(in_request, &labels);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  if (status == user_data_auth::CRYPTOHOME_ERROR_NOT_SET) {
    // The contents is |labels| is valid.
    *reply.mutable_labels() = {labels.begin(), labels.end()};
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::GetKeyData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetKeyDataReply>> response,
    const user_data_auth::GetKeyDataRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoGetKeyData, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::GetKeyDataReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoGetKeyData(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetKeyDataReply>> response,
    const user_data_auth::GetKeyDataRequest& in_request) {
  user_data_auth::GetKeyDataReply reply;
  cryptohome::KeyData data_out;
  bool found = false;
  auto status = service_->GetKeyData(in_request, &data_out, &found);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  if (reply.error() == user_data_auth::CRYPTOHOME_ERROR_NOT_SET && found) {
    *reply.add_key_data() = data_out;
  }
  response->Return(reply);
}

void UserDataAuthAdaptor::CheckKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckKeyReply>> response,
    const user_data_auth::CheckKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoCheckKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::CheckKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoCheckKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CheckKeyReply>> response,
    const user_data_auth::CheckKeyRequest& in_request) {
  user_data_auth::CheckKeyReply reply;
  auto status = service_->CheckKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::AddKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddKeyReply>> response,
    const user_data_auth::AddKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoAddKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<
              user_data_auth::AddKeyReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoAddKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddKeyReply>> response,
    const user_data_auth::AddKeyRequest& in_request) {
  user_data_auth::AddKeyReply reply;
  auto status = service_->AddKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::UpdateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateKeyReply>> response,
    const user_data_auth::UpdateKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoUpdateKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::UpdateKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoUpdateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateKeyReply>> response,
    const user_data_auth::UpdateKeyRequest& in_request) {
  user_data_auth::UpdateKeyReply reply;
  auto status = service_->UpdateKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::RemoveKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveKeyReply>> response,
    const user_data_auth::RemoveKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRemoveKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::RemoveKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemoveKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveKeyReply>> response,
    const user_data_auth::RemoveKeyRequest& in_request) {
  user_data_auth::RemoveKeyReply reply;
  auto status = service_->RemoveKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::MigrateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateKeyReply>> response,
    const user_data_auth::MigrateKeyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoMigrateKey, base::Unretained(this),
          ThreadSafeDBusMethodResponse<user_data_auth::MigrateKeyReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoMigrateKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateKeyReply>> response,
    const user_data_auth::MigrateKeyRequest& in_request) {
  user_data_auth::MigrateKeyReply reply;
  auto status = service_->MigrateKey(in_request);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void UserDataAuthAdaptor::StartMigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartMigrateToDircryptoReply>> response,
    const user_data_auth::StartMigrateToDircryptoRequest& in_request) {
  // This will be called whenever there's a status update from the migration.
  auto status_callback = base::Bind(
      [](UserDataAuthAdaptor* adaptor,
         const user_data_auth::DircryptoMigrationProgress& progress) {
        adaptor->SendDircryptoMigrationProgressSignal(progress);
      },
      base::Unretained(this));

  // Kick start the migration process.
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::Bind(&UserDataAuth::StartMigrateToDircrypto,
                 base::Unretained(service_), in_request, status_callback));

  // This function returns immediately after starting the migration process.
  // Also, this is always successful. Failure will be notified through the
  // signal.
  user_data_auth::StartMigrateToDircryptoReply reply;
  response->Return(reply);
}

void UserDataAuthAdaptor::NeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::NeedsDircryptoMigrationReply>> response,
    const user_data_auth::NeedsDircryptoMigrationRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoNeedsDircryptoMigration,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::NeedsDircryptoMigrationReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoNeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::NeedsDircryptoMigrationReply>> response,
    const user_data_auth::NeedsDircryptoMigrationRequest& in_request) {
  user_data_auth::NeedsDircryptoMigrationReply reply;
  bool result = false;
  auto status =
      service_->NeedsDircryptoMigration(in_request.account_id(), &result);
  // Note, if there's no error, then |status| is set to CRYPTOHOME_ERROR_NOT_SET
  // to indicate that.
  reply.set_error(status);
  reply.set_needs_dircrypto_migration(result);
  response->Return(reply);
}

void UserDataAuthAdaptor::GetSupportedKeyPolicies(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSupportedKeyPoliciesReply>> response,
    const user_data_auth::GetSupportedKeyPoliciesRequest& in_request) {
  user_data_auth::GetSupportedKeyPoliciesReply reply;
  reply.set_low_entropy_credentials_supported(
      service_->IsLowEntropyCredentialSupported());
  response->Return(reply);
}

void UserDataAuthAdaptor::GetAccountDiskUsage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAccountDiskUsageReply>> response,
    const user_data_auth::GetAccountDiskUsageRequest& in_request) {
  // Note that this is a long running call, so we're posting it to mount thread.
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoGetAccountDiskUsage,
                                base::Unretained(this),
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::GetAccountDiskUsageReply>::
                                    MakeThreadSafe(std::move(response)),
                                in_request));
}

void UserDataAuthAdaptor::DoGetAccountDiskUsage(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAccountDiskUsageReply>> response,
    const user_data_auth::GetAccountDiskUsageRequest& in_request) {
  user_data_auth::GetAccountDiskUsageReply reply;
  // Note that for now, this call always succeeds, so |reply.error| is unset.
  reply.set_size(service_->GetAccountDiskUsage(in_request.identifier()));
  response->Return(reply);
}

void ArcQuotaAdaptor::GetArcDiskFeatures(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetArcDiskFeaturesReply>> response,
    const user_data_auth::GetArcDiskFeaturesRequest& in_request) {
  user_data_auth::GetArcDiskFeaturesReply reply;
  reply.set_quota_supported(service_->IsArcQuotaSupported());
  response->Return(reply);
}

void ArcQuotaAdaptor::GetCurrentSpaceForArcUid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetCurrentSpaceForArcUidReply>> response,
    const user_data_auth::GetCurrentSpaceForArcUidRequest& in_request) {
  user_data_auth::GetCurrentSpaceForArcUidReply reply;
  reply.set_cur_space(service_->GetCurrentSpaceForArcUid(in_request.uid()));
  response->Return(reply);
}

void ArcQuotaAdaptor::GetCurrentSpaceForArcGid(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetCurrentSpaceForArcGidReply>> response,
    const user_data_auth::GetCurrentSpaceForArcGidRequest& in_request) {
  user_data_auth::GetCurrentSpaceForArcGidReply reply;
  reply.set_cur_space(service_->GetCurrentSpaceForArcGid(in_request.gid()));
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&Pkcs11Adaptor::DoPkcs11IsTpmTokenReady,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::Pkcs11IsTpmTokenReadyReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void Pkcs11Adaptor::DoPkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) {
  user_data_auth::Pkcs11IsTpmTokenReadyReply reply;
  reply.set_ready(service_->Pkcs11IsTpmTokenReady());
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11GetTpmTokeInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11GetTpmTokeInfoReply>> response,
    const user_data_auth::Pkcs11GetTpmTokeInfoRequest& in_request) {
  user_data_auth::Pkcs11GetTpmTokeInfoReply reply;
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11TerminateReply>> response,
    const user_data_auth::Pkcs11TerminateRequest& in_request) {
  user_data_auth::Pkcs11TerminateReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesGet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetReply>> response,
    const user_data_auth::InstallAttributesGetRequest& in_request) {
  user_data_auth::InstallAttributesGetReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesSet(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesSetReply>> response,
    const user_data_auth::InstallAttributesSetRequest& in_request) {
  user_data_auth::InstallAttributesSetReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesFinalize(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesFinalizeReply>> response,
    const user_data_auth::InstallAttributesFinalizeRequest& in_request) {
  user_data_auth::InstallAttributesFinalizeReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::InstallAttributesGetStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InstallAttributesGetStatusReply>> response,
    const user_data_auth::InstallAttributesGetStatusRequest& in_request) {
  user_data_auth::InstallAttributesGetStatusReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::GetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetFirmwareManagementParametersReply>> response,
    const user_data_auth::GetFirmwareManagementParametersRequest& in_request) {
  user_data_auth::GetFirmwareManagementParametersReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::RemoveFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveFirmwareManagementParametersReply>> response,
    const user_data_auth::RemoveFirmwareManagementParametersRequest&
        in_request) {
  user_data_auth::RemoveFirmwareManagementParametersReply reply;
  response->Return(reply);
}

void InstallAttributesAdaptor::SetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::SetFirmwareManagementParametersReply>> response,
    const user_data_auth::SetFirmwareManagementParametersRequest& in_request) {
  user_data_auth::SetFirmwareManagementParametersReply reply;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSystemSalt(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSystemSaltReply>> response,
    const user_data_auth::GetSystemSaltRequest& in_request) {
  user_data_auth::GetSystemSaltReply reply;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::UpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
    const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
        in_request) {
  user_data_auth::UpdateCurrentUserActivityTimestampReply reply;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSanitizedUsername(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSanitizedUsernameReply>> response,
    const user_data_auth::GetSanitizedUsernameRequest& in_request) {
  user_data_auth::GetSanitizedUsernameReply reply;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetLoginStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetLoginStatusReply>> response,
    const user_data_auth::GetLoginStatusRequest& in_request) {
  user_data_auth::GetLoginStatusReply reply;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetStatusString(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetStatusStringReply>> response,
    const user_data_auth::GetStatusStringRequest& in_request) {
  user_data_auth::GetStatusStringReply reply;
  response->Return(reply);
}

}  // namespace cryptohome
