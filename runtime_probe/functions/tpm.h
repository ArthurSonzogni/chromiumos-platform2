// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_TPM_H_
#define RUNTIME_PROBE_FUNCTIONS_TPM_H_

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class TpmFunction : public ProbeFunction {
  using ProbeFunction::ProbeFunction;

 public:
  NAME_PROBE_FUNCTION("tpm");

 private:
  void EvalAsyncImpl(base::OnceCallback<void(DataType)>) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_TPM_H_
