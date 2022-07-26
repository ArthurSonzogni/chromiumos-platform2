// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/state.h"

#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm_manager_error.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<bool> StateTpm1::IsEnabled() {
  tpm_manager::GetTpmNonsensitiveStatusRequest request;
  tpm_manager::GetTpmNonsensitiveStatusReply reply;

  if (brillo::ErrorPtr err;
      !backend_.GetProxy().GetTpmManager().GetTpmNonsensitiveStatus(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(reply.status()));

  return reply.is_enabled();
}

StatusOr<bool> StateTpm1::IsReady() {
  tpm_manager::GetTpmNonsensitiveStatusRequest request;
  tpm_manager::GetTpmNonsensitiveStatusReply reply;

  if (brillo::ErrorPtr err;
      !backend_.GetProxy().GetTpmManager().GetTpmNonsensitiveStatus(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(reply.status()));

  return reply.is_owned();
}

Status StateTpm1::Prepare() {
  tpm_manager::TakeOwnershipRequest request;
  tpm_manager::TakeOwnershipReply reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmManager().TakeOwnership(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  return MakeStatus<TPMManagerError>(reply.status());
}

}  // namespace hwsec
