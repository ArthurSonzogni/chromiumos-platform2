// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_TPM_HANDLE_MANAGER_H_
#define VTPM_BACKENDS_TPM_HANDLE_MANAGER_H_

#include <vector>

#include <trunks/tpm_generated.h>

namespace vtpm {

// This interface manages mainly the following functions:
// 1. The usage of the virtual TPM handles,
// 2. The usage of the host TPM handles, and
// 3. the association between handles from 1. and 2.
class TpmHandleManager {
 public:
  virtual ~TpmHandleManager() = default;

  // Checks if `handle` is one of supported handle type by virtual TPM, for
  // virtual TPM only provides a subset of supported types among all types of
  // handles (see TPM2.0 spec Part 2 7.2 TPM_HT).
  virtual bool IsHandleTypeSuppoerted(trunks::TPM_HANDLE handle) = 0;

  // Gets the list of TPM handles of the same type of `starting_index` and
  // stores the resul in `found_handles`.
  virtual trunks::TPM_RC GetHandleList(
      trunks::TPM_HANDLE starting_handle,
      std::vector<trunks::TPM_HANDLE>* found_handles) = 0;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_TPM_HANDLE_MANAGER_H_
