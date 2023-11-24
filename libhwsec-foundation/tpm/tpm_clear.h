// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TPM_TPM_CLEAR_H_
#define LIBHWSEC_FOUNDATION_TPM_TPM_CLEAR_H_

#include <optional>

#include "libhwsec-foundation/hwsec-foundation_export.h"

// These functions support clearing TPM across different system.

namespace hwsec_foundation::tpm {

// Checks the device support TPM clear request or not.
HWSEC_FOUNDATION_EXPORT bool SupportClearRequest();

// Checks the device support TPM clear request without user prompt or not.
HWSEC_FOUNDATION_EXPORT bool SupportClearWithoutPrompt();

// Sets the value of "Clear TPM Request" to |value|.
HWSEC_FOUNDATION_EXPORT bool SetClearTpmRequest(bool value);

// Sets the value of "Clear TPM Request" to |value| and allow user prompt.
HWSEC_FOUNDATION_EXPORT bool SetClearTpmRequestAllowPrompt(bool value);

// Gets the current value of "Clear TPM Request".
HWSEC_FOUNDATION_EXPORT std::optional<bool> GetClearTpmRequest();

}  // namespace hwsec_foundation::tpm

#endif  // LIBHWSEC_FOUNDATION_TPM_TPM_CLEAR_H_
