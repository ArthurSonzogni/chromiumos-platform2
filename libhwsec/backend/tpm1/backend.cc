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

using hwsec_foundation::status::MakeStatus;
using trousers::ScopedTssContext;

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

  ScopedTssContext local_context_handle;

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(
                      overalls.Ospi_Context_Create(local_context_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Create");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_Connect(
                      local_context_handle, nullptr)))
      .WithStatus<TPMError>("Failed to call Ospi_Context_Connect");

  return local_context_handle;
}

StatusOr<BackendTpm1::TssTpmContext> BackendTpm1::GetTssUserContext() {
  if (tss_user_context_cache_.has_value()) {
    return tss_user_context_cache_.value();
  }

  ASSIGN_OR_RETURN(ScopedTssContext context, GetScopedTssContext(),
                   _.WithStatus<TPMError>("Failed to get scoped TSS context"));

  overalls::Overalls& overalls = overall_context_.overalls;

  TSS_HTPM local_tpm_handle;

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_GetTpmObject(
                      context.value(), &local_tpm_handle)))
      .WithStatus<TPMError>("Failed to call Ospi_Context_GetTpmObject");

  tss_user_context_ = std::move(context);
  tss_user_context_cache_ = TssTpmContext{
      .context = tss_user_context_.value(),
      .tpm_handle = local_tpm_handle,
  };
  return tss_user_context_cache_.value();
}

}  // namespace hwsec
