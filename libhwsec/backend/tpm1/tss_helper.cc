// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "libhwsec/backend/tpm1/tss_helper.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/error/tpm_manager_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

void DelegateHandleSettingCleanup(overalls::Overalls& overalls,
                                  TSS_HPOLICY tpm_usage_policy) {
  overalls.Ospi_SetAttribUint32(
      tpm_usage_policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
      TSS_TSPATTRIB_POLDEL_TYPE, TSS_DELEGATIONTYPE_NONE);
  overalls.Ospi_Policy_FlushSecret(tpm_usage_policy);
}

void OwnerHandleSettingCleanup(overalls::Overalls& overalls,
                               TSS_HPOLICY tpm_usage_policy) {
  overalls.Ospi_Policy_FlushSecret(tpm_usage_policy);
}

}  // namespace

StatusOr<ScopedTssContext> TssHelper::GetScopedTssContext() {
  ScopedTssContext local_context_handle(overalls_);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Context_Create(
                      local_context_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Create");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Context_Connect(
                      local_context_handle, nullptr)))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Connect");

  return local_context_handle;
}

StatusOr<TSS_HCONTEXT> TssHelper::GetTssContext() {
  if (tss_context_.has_value()) {
    return tss_context_.value().value();
  }

  ASSIGN_OR_RETURN(ScopedTssContext context, GetScopedTssContext(),
                   _.WithStatus<TPMError>("Failed to get scoped TSS context"));

  tss_context_ = std::move(context);
  return tss_context_.value().value();
}

StatusOr<TSS_HTPM> TssHelper::GetTpmHandle() {
  if (user_tpm_handle_.has_value()) {
    return user_tpm_handle_.value().value();
  }

  ASSIGN_OR_RETURN(TSS_HCONTEXT context, GetTssContext(),
                   _.WithStatus<TPMError>("Failed to get TSS context"));

  ScopedTssObject<TSS_HTPM> local_tpm_handle(overalls_, context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Context_GetTpmObject(
                      context, local_tpm_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_GetTpmObject");

  user_tpm_handle_ = std::move(local_tpm_handle);
  return user_tpm_handle_.value().value();
}

StatusOr<base::ScopedClosureRunner> TssHelper::SetTpmHandleAsDelegate() {
  ASSIGN_OR_RETURN(const tpm_manager::GetTpmStatusReply& reply,
                   GetTpmStatusReply());
  ASSIGN_OR_RETURN(TSS_HTPM local_tpm_handle, GetTpmHandle());
  return SetAsDelegate(local_tpm_handle, reply.local_data().owner_delegate());
}

StatusOr<base::ScopedClosureRunner> TssHelper::SetTpmHandleAsOwner() {
  ASSIGN_OR_RETURN(const tpm_manager::GetTpmStatusReply& reply,
                   GetTpmStatusReply());
  ASSIGN_OR_RETURN(TSS_HTPM local_tpm_handle, GetTpmHandle());
  return SetAsOwner(local_tpm_handle, reply.local_data().owner_password());
}

StatusOr<base::ScopedClosureRunner> TssHelper::SetTpmHandleByEkReadability() {
  ASSIGN_OR_RETURN(const tpm_manager::GetTpmStatusReply& reply,
                   GetTpmStatusReply());

  brillo::Blob delegate_blob =
      brillo::BlobFromString(reply.local_data().owner_delegate().blob());
  if (delegate_blob.empty()) {
    return MakeStatus<TPMError>("No valid owner delegate",
                                TPMRetryAction::kNoRetry);
  }
  ASSIGN_OR_RETURN(TSS_HTPM local_tpm_handle, GetTpmHandle());
  ASSIGN_OR_RETURN(bool can_read_ek, CanDelegateReadInternalPub(delegate_blob));

  base::ScopedClosureRunner tpm_handle_cleanup;
  if (can_read_ek) {
    ASSIGN_OR_RETURN(
        tpm_handle_cleanup,
        SetAsDelegate(local_tpm_handle, reply.local_data().owner_delegate()),
        _.WithStatus<TPMError>("Failed to set tpm handle as delegate"));
  } else {
    LOG(WARNING) << __func__ << ": owner delegate cannot read ek.";
    ASSIGN_OR_RETURN(
        tpm_handle_cleanup,
        SetAsOwner(local_tpm_handle, reply.local_data().owner_password()),
        _.WithStatus<TPMError>("Failed to set tpm handle as owner"));
  }
  return tpm_handle_cleanup;
}

StatusOr<tpm_manager::GetTpmStatusReply> TssHelper::GetTpmStatusReply() {
  tpm_manager::GetTpmStatusRequest request;
  tpm_manager::GetTpmStatusReply reply;

  if (brillo::ErrorPtr err; !tpm_manager_.GetTpmStatus(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }
  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(reply.status()));
  return reply;
}

StatusOr<base::ScopedClosureRunner> TssHelper::SetAsDelegate(
    TSS_HTPM local_tpm_handle,
    const tpm_manager::AuthDelegate& owner_delegate) {
  if (owner_delegate.blob().empty() || owner_delegate.secret().empty()) {
    return MakeStatus<TPMError>("No valid owner delegate",
                                TPMRetryAction::kNoRetry);
  }

  TSS_HPOLICY tpm_usage_policy;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_GetPolicyObject(
                      local_tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy)))
      .WithStatus<TPMError>("Failed to call Ospi_GetPolicyObject");

  base::ScopedClosureRunner cleanup(base::BindOnce(
      DelegateHandleSettingCleanup, std::ref(overalls_), tpm_usage_policy));

  brillo::Blob delegate_secret =
      brillo::BlobFromString(owner_delegate.secret());
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Policy_SetSecret(
                      tpm_usage_policy, TSS_SECRET_MODE_PLAIN,
                      delegate_secret.size(), delegate_secret.data())))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_SetSecret");

  brillo::Blob delegate_blob = brillo::BlobFromString(owner_delegate.blob());
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_SetAttribData(
                      tpm_usage_policy, TSS_TSPATTRIB_POLICY_DELEGATION_INFO,
                      TSS_TSPATTRIB_POLDEL_OWNERBLOB, delegate_blob.size(),
                      delegate_blob.data())))
      .WithStatus<TPMError>("Failed to call Ospi_SetAttribData");

  return cleanup;
}

StatusOr<base::ScopedClosureRunner> TssHelper::SetAsOwner(
    TSS_HTPM local_tpm_handle, const std::string& owner_password) {
  if (owner_password.empty()) {
    return MakeStatus<TPMError>("No valid owner password",
                                TPMRetryAction::kNoRetry);
  }

  TSS_HPOLICY tpm_usage_policy;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_GetPolicyObject(
                      local_tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy)))
      .WithStatus<TPMError>("Failed to call Ospi_GetPolicyObject");

  base::ScopedClosureRunner cleanup(base::BindOnce(
      OwnerHandleSettingCleanup, std::ref(overalls_), tpm_usage_policy));

  brillo::Blob owner_password_blob = brillo::BlobFromString(owner_password);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Policy_SetSecret(
                      tpm_usage_policy, TSS_SECRET_MODE_PLAIN,
                      owner_password_blob.size(), owner_password_blob.data())))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_SetSecret");
  return cleanup;
}

StatusOr<bool> TssHelper::CanDelegateReadInternalPub(
    brillo::Blob& delegate_blob) {
  UINT64 offset = 0;
  TPM_DELEGATE_OWNER_BLOB owner_blob = {};
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(
                      overalls_.Orspi_UnloadBlob_TPM_DELEGATE_OWNER_BLOB_s(
                          &offset, delegate_blob.data(), delegate_blob.size(),
                          &owner_blob)))
      .WithStatus<TPMError>(
          "Failed to call Orspi_UnloadBlob_TPM_DELEGATE_OWNER_BLOB");
  free(owner_blob.pub.pcrInfo.pcrSelection.pcrSelect);
  free(owner_blob.additionalArea);
  free(owner_blob.sensitiveArea);
  if (offset != delegate_blob.size()) {
    return MakeStatus<TPMError>("Bad delegate blob", TPMRetryAction::kNoRetry);
  }
  return owner_blob.pub.permissions.per1 & TPM_DELEGATE_OwnerReadInternalPub;
}

}  // namespace hwsec
