// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

BackendTpm1::BackendTpm1(Proxy& proxy,
                         MiddlewareDerivative middleware_derivative)
    : proxy_(proxy),
      overall_context_(OverallsContext{
          .overalls = proxy_.GetOveralls(),
      }),
      middleware_derivative_(middleware_derivative) {}

BackendTpm1::~BackendTpm1() {}

StatusOr<ScopedTssContext> BackendTpm1::GetScopedTssContext() {
  overalls::Overalls& overalls = overall_context_.overalls;

  ScopedTssContext local_context_handle(overalls);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(
                      overalls.Ospi_Context_Create(local_context_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Create");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_Connect(
                      local_context_handle, nullptr)))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Connect");

  return local_context_handle;
}

StatusOr<TSS_HCONTEXT> BackendTpm1::GetTssContext() {
  if (tss_context_.has_value()) {
    return tss_context_.value().value();
  }

  ASSIGN_OR_RETURN(ScopedTssContext context, GetScopedTssContext(),
                   _.WithStatus<TPMError>("Failed to get scoped TSS context"));

  tss_context_ = std::move(context);
  return tss_context_.value().value();
}

StatusOr<TSS_HTPM> BackendTpm1::GetUserTpmHandle() {
  if (user_tpm_handle_.has_value()) {
    return user_tpm_handle_.value().value();
  }

  ASSIGN_OR_RETURN(TSS_HCONTEXT context, GetTssContext(),
                   _.WithStatus<TPMError>("Failed to get TSS context"));

  overalls::Overalls& overalls = overall_context_.overalls;

  ScopedTssObject<TSS_HTPM> local_tpm_handle(overalls, context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_GetTpmObject(
                      context, local_tpm_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_GetTpmObject");

  user_tpm_handle_ = std::move(local_tpm_handle);
  return user_tpm_handle_.value().value();
}

}  // namespace hwsec
