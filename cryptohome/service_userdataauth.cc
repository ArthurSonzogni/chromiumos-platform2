// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/service_userdataauth.h"

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/cryptohome.h>
#include <chromeos/constants/cryptohome.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/utility/task_dispatching_framework.h>

#include "cryptohome/userdataauth.h"

namespace cryptohome {

using ::hwsec_foundation::utility::ThreadSafeDBusMethodResponse;

void UserDataAuthAdaptor::IsMounted(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::IsMountedReply>> response,
    const user_data_auth::IsMountedRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoIsMounted, weak_factory_.GetWeakPtr(),
          Username(in_request.username()),
          ThreadSafeDBusMethodResponse<user_data_auth::IsMountedReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoIsMounted(
    const Username& username,
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

void UserDataAuthAdaptor::GetVaultProperties(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetVaultPropertiesReply>> response,
    const user_data_auth::GetVaultPropertiesRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoGetVaultProperties,
                                weak_factory_.GetWeakPtr(), in_request,
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::GetVaultPropertiesReply>::
                                    MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoGetVaultProperties(
    const user_data_auth::GetVaultPropertiesRequest& in_request,
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetVaultPropertiesReply>> response) {
  std::move(response)->Return(service_->GetVaultProperties(in_request));
}

void UserDataAuthAdaptor::Unmount(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UnmountReply>> response,
    const user_data_auth::UnmountRequest& in_request) {
  // Unmount request doesn't have any parameters
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoUnmount, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::UnmountReply>::
              MakeThreadSafe(std::move(response))));
}

void UserDataAuthAdaptor::DoUnmount(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<user_data_auth::UnmountReply>>
        response) {
  user_data_auth::UnmountReply reply = service_->Unmount();
  response->Return(reply);
}

void UserDataAuthAdaptor::StartAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartAuthSessionReply>> response,
    const user_data_auth::StartAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoStartAuthSession, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::StartAuthSessionReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoStartAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartAuthSessionReply>> response,
    const user_data_auth::StartAuthSessionRequest& in_request) {
  service_->StartAuthSession(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::StartAuthSessionReply>> local_response,
             const user_data_auth::StartAuthSessionReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::InvalidateAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InvalidateAuthSessionReply>> response,
    const user_data_auth::InvalidateAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoInvalidateAuthSession,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::InvalidateAuthSessionReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoInvalidateAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::InvalidateAuthSessionReply>> response,
    const user_data_auth::InvalidateAuthSessionRequest& in_request) {
  service_->InvalidateAuthSession(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::InvalidateAuthSessionReply>> local_response,
             const user_data_auth::InvalidateAuthSessionReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::ExtendAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ExtendAuthSessionReply>> response,
    const user_data_auth::ExtendAuthSessionRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoExtendAuthSession, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::ExtendAuthSessionReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoExtendAuthSession(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ExtendAuthSessionReply>> response,
    const user_data_auth::ExtendAuthSessionRequest& in_request) {
  service_->ExtendAuthSession(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::ExtendAuthSessionReply>> local_response,
             const user_data_auth::ExtendAuthSessionReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::CreatePersistentUser(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CreatePersistentUserReply>> response,
    const user_data_auth::CreatePersistentUserRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoCreatePersistentUser,
                                weak_factory_.GetWeakPtr(),
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::CreatePersistentUserReply>::
                                    MakeThreadSafe(std::move(response)),
                                in_request));
}

void UserDataAuthAdaptor::DoCreatePersistentUser(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CreatePersistentUserReply>> response,
    const user_data_auth::CreatePersistentUserRequest& in_request) {
  service_->CreatePersistentUser(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::CreatePersistentUserReply>> local_response,
             const user_data_auth::CreatePersistentUserReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::PrepareGuestVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareGuestVaultReply>> response,
    const user_data_auth::PrepareGuestVaultRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoPrepareGuestVault, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::PrepareGuestVaultReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoPrepareGuestVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareGuestVaultReply>> response,
    const user_data_auth::PrepareGuestVaultRequest& in_request) {
  service_->PrepareGuestVault(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::PrepareGuestVaultReply>> local_response,
             const user_data_auth::PrepareGuestVaultReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::PrepareEphemeralVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareEphemeralVaultReply>> response,
    const user_data_auth::PrepareEphemeralVaultRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoPrepareEphemeralVault,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::PrepareEphemeralVaultReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoPrepareEphemeralVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareEphemeralVaultReply>> response,
    const user_data_auth::PrepareEphemeralVaultRequest& in_request) {
  service_->PrepareEphemeralVault(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::PrepareEphemeralVaultReply>> local_response,
             const user_data_auth::PrepareEphemeralVaultReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::PreparePersistentVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PreparePersistentVaultReply>> response,
    const user_data_auth::PreparePersistentVaultRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoPreparePersistentVault,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::PreparePersistentVaultReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoPreparePersistentVault(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PreparePersistentVaultReply>> response,
    const user_data_auth::PreparePersistentVaultRequest& in_request) {
  service_->PreparePersistentVault(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::PreparePersistentVaultReply>> local_response,
             const user_data_auth::PreparePersistentVaultReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::PrepareVaultForMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareVaultForMigrationReply>> response,
    const user_data_auth::PrepareVaultForMigrationRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoPrepareVaultForMigration,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::PrepareVaultForMigrationReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoPrepareVaultForMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareVaultForMigrationReply>> response,
    const user_data_auth::PrepareVaultForMigrationRequest& in_request) {
  service_->PrepareVaultForMigration(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::PrepareVaultForMigrationReply>> local_response,
             const user_data_auth::PrepareVaultForMigrationReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::AddAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddAuthFactorReply>> response,
    const user_data_auth::AddAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoAddAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::AddAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoAddAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AddAuthFactorReply>> response,
    const user_data_auth::AddAuthFactorRequest& in_request) {
  service_->AddAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::AddAuthFactorReply>> local_response,
             const user_data_auth::AddAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::AuthenticateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AuthenticateAuthFactorReply>> response,
    const user_data_auth::AuthenticateAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoAuthenticateAuthFactor,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::AuthenticateAuthFactorReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoAuthenticateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::AuthenticateAuthFactorReply>> response,
    const user_data_auth::AuthenticateAuthFactorRequest& in_request) {
  service_->AuthenticateAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::AuthenticateAuthFactorReply>> local_response,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::UpdateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateAuthFactorReply>> response,
    const user_data_auth::UpdateAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoUpdateAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::UpdateAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoUpdateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateAuthFactorReply>> response,
    const user_data_auth::UpdateAuthFactorRequest& in_request) {
  service_->UpdateAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::UpdateAuthFactorReply>> local_response,
             const user_data_auth::UpdateAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::UpdateAuthFactorMetadata(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateAuthFactorMetadataReply>> response,
    const user_data_auth::UpdateAuthFactorMetadataRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoUpdateAuthFactorMetadata,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::UpdateAuthFactorMetadataReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoUpdateAuthFactorMetadata(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateAuthFactorMetadataReply>> response,
    const user_data_auth::UpdateAuthFactorMetadataRequest& in_request) {
  service_->UpdateAuthFactorMetadata(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::UpdateAuthFactorMetadataReply>> local_response,
             const user_data_auth::UpdateAuthFactorMetadataReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::RelabelAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RelabelAuthFactorReply>> response,
    const user_data_auth::RelabelAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRelabelAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::RelabelAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRelabelAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RelabelAuthFactorReply>> response,
    const user_data_auth::RelabelAuthFactorRequest& in_request) {
  service_->RelabelAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::RelabelAuthFactorReply>> local_response,
             const user_data_auth::RelabelAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::ReplaceAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ReplaceAuthFactorReply>> response,
    const user_data_auth::ReplaceAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoReplaceAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::ReplaceAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoReplaceAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ReplaceAuthFactorReply>> response,
    const user_data_auth::ReplaceAuthFactorRequest& in_request) {
  service_->ReplaceAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::ReplaceAuthFactorReply>> local_response,
             const user_data_auth::ReplaceAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::RemoveAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveAuthFactorReply>> response,
    const user_data_auth::RemoveAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoRemoveAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::RemoveAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemoveAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveAuthFactorReply>> response,
    const user_data_auth::RemoveAuthFactorRequest& in_request) {
  service_->RemoveAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::RemoveAuthFactorReply>> local_response,
             const user_data_auth::RemoveAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::ListAuthFactors(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListAuthFactorsReply>> response,
    const user_data_auth::ListAuthFactorsRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoListAuthFactors, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::ListAuthFactorsReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoListAuthFactors(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ListAuthFactorsReply>> response,
    const user_data_auth::ListAuthFactorsRequest& in_request) {
  service_->ListAuthFactors(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::ListAuthFactorsReply>> local_response,
             const user_data_auth::ListAuthFactorsReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::GetAuthFactorExtendedInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAuthFactorExtendedInfoReply>> response,
    const user_data_auth::GetAuthFactorExtendedInfoRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoGetAuthFactorExtendedInfo,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::GetAuthFactorExtendedInfoReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoGetAuthFactorExtendedInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAuthFactorExtendedInfoReply>> response,
    const user_data_auth::GetAuthFactorExtendedInfoRequest& in_request) {
  service_->GetAuthFactorExtendedInfo(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::GetAuthFactorExtendedInfoReply>>
                 local_response,
             const user_data_auth::GetAuthFactorExtendedInfoReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::PrepareAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareAuthFactorReply>> response,
    const user_data_auth::PrepareAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoPrepareAuthFactor, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::PrepareAuthFactorReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoPrepareAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::PrepareAuthFactorReply>> response,
    const user_data_auth::PrepareAuthFactorRequest& in_request) {
  service_->PrepareAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::PrepareAuthFactorReply>> local_response,
             const user_data_auth::PrepareAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::TerminateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::TerminateAuthFactorReply>> response,
    const user_data_auth::TerminateAuthFactorRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoTerminateAuthFactor,
                                weak_factory_.GetWeakPtr(),
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::TerminateAuthFactorReply>::
                                    MakeThreadSafe(std::move(response)),
                                in_request));
}

void UserDataAuthAdaptor::DoTerminateAuthFactor(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::TerminateAuthFactorReply>> response,
    const user_data_auth::TerminateAuthFactorRequest& in_request) {
  service_->TerminateAuthFactor(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::TerminateAuthFactorReply>> local_response,
             const user_data_auth::TerminateAuthFactorReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::LockFactorUntilReboot(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::LockFactorUntilRebootReply>> response,
    const user_data_auth::LockFactorUntilRebootRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoLockFactorUntilReboot,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::LockFactorUntilRebootReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoLockFactorUntilReboot(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::LockFactorUntilRebootReply>> response,
    const user_data_auth::LockFactorUntilRebootRequest& in_request) {
  service_->LockFactorUntilReboot(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::LockFactorUntilRebootReply>> local_response,
             const user_data_auth::LockFactorUntilRebootReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::CreateVaultKeyset(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CreateVaultKeysetReply>> response,
    const user_data_auth::CreateVaultKeysetRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoCreateVaultKeyset, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::CreateVaultKeysetReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoCreateVaultKeyset(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::CreateVaultKeysetReply>> response,
    const user_data_auth::CreateVaultKeysetRequest& in_request) {
  service_->CreateVaultKeyset(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::CreateVaultKeysetReply>> local_response,
             const user_data_auth::CreateVaultKeysetReply& reply) {
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
          &UserDataAuthAdaptor::DoRemove, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<
              user_data_auth::RemoveReply>::MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoRemove(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::RemoveReply>> response,
    const user_data_auth::RemoveRequest& in_request) {
  service_->Remove(
      in_request, base::BindOnce(
                      [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                             user_data_auth::RemoveReply>> local_response,
                         const user_data_auth::RemoveReply& reply) {
                        local_response->Return(reply);
                      },
                      std::move(response)));
}

void UserDataAuthAdaptor::GetWebAuthnSecret(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretReply>> response,
    const user_data_auth::GetWebAuthnSecretRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &UserDataAuthAdaptor::DoGetWebAuthnSecret, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::GetWebAuthnSecretReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void UserDataAuthAdaptor::DoGetWebAuthnSecret(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretReply>> response,
    const user_data_auth::GetWebAuthnSecretRequest& in_request) {
  response->Return(service_->GetWebAuthnSecret(in_request));
}

void UserDataAuthAdaptor::GetWebAuthnSecretHash(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretHashReply>> response,
    const user_data_auth::GetWebAuthnSecretHashRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoGetWebAuthnSecretHash,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::GetWebAuthnSecretHashReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoGetWebAuthnSecretHash(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetWebAuthnSecretHashReply>> response,
    const user_data_auth::GetWebAuthnSecretHashRequest& in_request) {
  response->Return(service_->GetWebAuthnSecretHash(in_request));
}

void UserDataAuthAdaptor::GetRecoverableKeyStores(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetRecoverableKeyStoresReply>> response,
    const user_data_auth::GetRecoverableKeyStoresRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoGetRecoverableKeyStores,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::GetRecoverableKeyStoresReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoGetRecoverableKeyStores(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetRecoverableKeyStoresReply>> response,
    const user_data_auth::GetRecoverableKeyStoresRequest& in_request) {
  service_->GetRecoverableKeyStores(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::GetRecoverableKeyStoresReply>> local_response,
             const user_data_auth::GetRecoverableKeyStoresReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::StartMigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartMigrateToDircryptoReply>> response,
    const user_data_auth::StartMigrateToDircryptoRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoStartMigrateToDircrypto,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::StartMigrateToDircryptoReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoStartMigrateToDircrypto(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::StartMigrateToDircryptoReply>> response,
    const user_data_auth::StartMigrateToDircryptoRequest& in_request) {
  // This will be called whenever there's a status update from the migration.
  auto status_callback = base::BindRepeating(
      [](base::WeakPtr<UserDataAuthAdaptor> adaptor,
         const user_data_auth::DircryptoMigrationProgress& progress) {
        if (adaptor) {
          adaptor->SendDircryptoMigrationProgressSignal(progress);
        }
      },
      weak_factory_.GetWeakPtr());

  // Kick start the migration process.
  service_->StartMigrateToDircrypto(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::StartMigrateToDircryptoReply>> local_response,
             const user_data_auth::StartMigrateToDircryptoReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)),
      status_callback);
}

void UserDataAuthAdaptor::NeedsDircryptoMigration(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::NeedsDircryptoMigrationReply>> response,
    const user_data_auth::NeedsDircryptoMigrationRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoNeedsDircryptoMigration,
                     weak_factory_.GetWeakPtr(),
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
                                weak_factory_.GetWeakPtr(),
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

void Pkcs11Adaptor::Pkcs11IsTpmTokenReady(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
    const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&Pkcs11Adaptor::DoPkcs11IsTpmTokenReady,
                     weak_factory_.GetWeakPtr(),
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

void Pkcs11Adaptor::Pkcs11GetTpmTokenInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11GetTpmTokenInfoReply>> response,
    const user_data_auth::Pkcs11GetTpmTokenInfoRequest& in_request) {
  user_data_auth::Pkcs11GetTpmTokenInfoReply reply;
  *reply.mutable_token_info() =
      service_->Pkcs11GetTpmTokenInfo(Username(in_request.username()));
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11TerminateReply>> response,
    const user_data_auth::Pkcs11TerminateRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &Pkcs11Adaptor::DoPkcs11Terminate, weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::Pkcs11TerminateReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void Pkcs11Adaptor::DoPkcs11Terminate(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11TerminateReply>> response,
    const user_data_auth::Pkcs11TerminateRequest& in_request) {
  user_data_auth::Pkcs11TerminateReply reply;
  service_->Pkcs11Terminate();
  response->Return(reply);
}

void Pkcs11Adaptor::Pkcs11RestoreTpmTokens(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11RestoreTpmTokensReply>> response,
    const user_data_auth::Pkcs11RestoreTpmTokensRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&Pkcs11Adaptor::DoPkcs11RestoreTpmTokens,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::Pkcs11RestoreTpmTokensReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void Pkcs11Adaptor::DoPkcs11RestoreTpmTokens(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::Pkcs11RestoreTpmTokensReply>> response,
    const user_data_auth::Pkcs11RestoreTpmTokensRequest& in_request) {
  user_data_auth::Pkcs11RestoreTpmTokensReply reply;
  service_->Pkcs11RestoreTpmTokens();
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSystemSalt(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSystemSaltReply>> response,
    const user_data_auth::GetSystemSaltRequest& in_request) {
  user_data_auth::GetSystemSaltReply reply;
  const brillo::SecureBlob& salt = service_->GetSystemSalt();
  reply.set_salt(salt.char_data(), salt.size());
  response->Return(reply);
}

void CryptohomeMiscAdaptor::UpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
    const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
        in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &CryptohomeMiscAdaptor::DoUpdateCurrentUserActivityTimestamp,
          weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<
              user_data_auth::UpdateCurrentUserActivityTimestampReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void CryptohomeMiscAdaptor::DoUpdateCurrentUserActivityTimestamp(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
    const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
        in_request) {
  user_data_auth::UpdateCurrentUserActivityTimestampReply reply;
  bool success =
      service_->UpdateCurrentUserActivityTimestamp(in_request.time_shift_sec());
  if (!success) {
    reply.set_error(
        user_data_auth::CRYPTOHOME_ERROR_UPDATE_USER_ACTIVITY_TIMESTAMP_FAILED);
  }
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetSanitizedUsername(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetSanitizedUsernameReply>> response,
    const user_data_auth::GetSanitizedUsernameRequest& in_request) {
  user_data_auth::GetSanitizedUsernameReply reply;
  reply.set_sanitized_username(*brillo::cryptohome::home::SanitizeUserName(
      Username(in_request.username())));
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetLoginStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetLoginStatusReply>> response,
    const user_data_auth::GetLoginStatusRequest& in_request) {
  user_data_auth::GetLoginStatusReply reply;
  reply.set_owner_user_exists(service_->OwnerUserExists());
  reply.set_is_locked_to_single_user(
      base::PathExists(base::FilePath(kLockedToSingleUserFile)));
  response->Return(reply);
}

void CryptohomeMiscAdaptor::LockToSingleUserMountUntilReboot(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::LockToSingleUserMountUntilRebootReply>> response,
    const user_data_auth::LockToSingleUserMountUntilRebootRequest& in_request) {
  user_data_auth::LockToSingleUserMountUntilRebootReply reply;
  auto status =
      service_->LockToSingleUserMountUntilReboot(in_request.account_id());
  reply.set_error(status);
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetRsuDeviceId(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetRsuDeviceIdReply>> response,
    const user_data_auth::GetRsuDeviceIdRequest& in_request) {
  user_data_auth::GetRsuDeviceIdReply reply;
  std::string rsu_device_id;
  if (!service_->GetRsuDeviceId(&rsu_device_id)) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             DBUS_ERROR_FAILED,
                             "Unable to retrieve lookup key!");
    return;
  }
  *reply.mutable_rsu_device_id() = rsu_device_id;
  response->Return(reply);
}

void CryptohomeMiscAdaptor::GetPinWeaverInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetPinWeaverInfoReply>> response,
    const user_data_auth::GetPinWeaverInfoRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(
          &CryptohomeMiscAdaptor::DoGetPinWeaverInfo,
          weak_factory_.GetWeakPtr(),
          ThreadSafeDBusMethodResponse<user_data_auth::GetPinWeaverInfoReply>::
              MakeThreadSafe(std::move(response)),
          in_request));
}

void CryptohomeMiscAdaptor::DoGetPinWeaverInfo(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetPinWeaverInfoReply>> response,
    const user_data_auth::GetPinWeaverInfoRequest& in_request) {
  response->Return(service_->GetPinWeaverInfo());
}

void UserDataAuthAdaptor::GetAuthSessionStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAuthSessionStatusReply>> response,
    const user_data_auth::GetAuthSessionStatusRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE, base::BindOnce(&UserDataAuthAdaptor::DoGetAuthSessionStatus,
                                weak_factory_.GetWeakPtr(),
                                ThreadSafeDBusMethodResponse<
                                    user_data_auth::GetAuthSessionStatusReply>::
                                    MakeThreadSafe(std::move(response)),
                                in_request));
}

void UserDataAuthAdaptor::DoGetAuthSessionStatus(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetAuthSessionStatusReply>> response,
    const user_data_auth::GetAuthSessionStatusRequest& in_request) {
  service_->GetAuthSessionStatus(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::GetAuthSessionStatusReply>> local_response,
             const user_data_auth::GetAuthSessionStatusReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

void UserDataAuthAdaptor::ResetApplicationContainer(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ResetApplicationContainerReply>> response,
    const user_data_auth::ResetApplicationContainerRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoResetApplicationContainer,
                     weak_factory_.GetWeakPtr(),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::ResetApplicationContainerReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoResetApplicationContainer(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::ResetApplicationContainerReply>> response,
    const user_data_auth::ResetApplicationContainerRequest& in_request) {
  user_data_auth::ResetApplicationContainerReply reply =
      service_->ResetApplicationContainer(in_request);
  response->Return(reply);
}

void UserDataAuthAdaptor::GetArcDiskFeatures(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::GetArcDiskFeaturesReply>> response,
    const user_data_auth::GetArcDiskFeaturesRequest& in_request) {
  user_data_auth::GetArcDiskFeaturesReply reply;
  reply.set_quota_supported(service_->IsArcQuotaSupported());
  response->Return(reply);
}

void UserDataAuthAdaptor::MigrateLegacyFingerprints(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateLegacyFingerprintsReply>> response,
    const user_data_auth::MigrateLegacyFingerprintsRequest& in_request) {
  service_->PostTaskToMountThread(
      FROM_HERE,
      base::BindOnce(&UserDataAuthAdaptor::DoMigrateLegacyFingerprints,
                     base::Unretained(this),
                     ThreadSafeDBusMethodResponse<
                         user_data_auth::MigrateLegacyFingerprintsReply>::
                         MakeThreadSafe(std::move(response)),
                     in_request));
}

void UserDataAuthAdaptor::DoMigrateLegacyFingerprints(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        user_data_auth::MigrateLegacyFingerprintsReply>> response,
    const user_data_auth::MigrateLegacyFingerprintsRequest& in_request) {
  service_->MigrateLegacyFingerprints(
      in_request,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::MigrateLegacyFingerprintsReply>>
                 local_response,
             const user_data_auth::MigrateLegacyFingerprintsReply& reply) {
            local_response->Return(reply);
          },
          std::move(response)));
}

}  // namespace cryptohome
