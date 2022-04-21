// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

#include <base/callback_helpers.h>
#include <base/strings/stringprintf.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using RandomTpm1 = BackendTpm1::RandomTpm1;

StatusOr<brillo::Blob> RandomTpm1::RandomBlob(size_t size) {
  ASSIGN_OR_RETURN(const brillo::SecureBlob& blob, RandomSecureBlob(size),
                   _.WithStatus<TPMError>("Failed to get random secure data"));

  return brillo::Blob(blob.begin(), blob.end());
}

StatusOr<brillo::SecureBlob> RandomTpm1::RandomSecureBlob(size_t size) {
  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  brillo::SecureBlob random(size);
  trousers::ScopedTssMemory tpm_data(user_context.context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_TPM_GetRandom(
                      user_context.tpm_handle, random.size(), tpm_data.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_TPM_GetRandom");

  memcpy(random.data(), tpm_data.value(), random.size());
  brillo::SecureClearBytes(tpm_data.value(), random.size());
  return random;
}

}  // namespace hwsec
