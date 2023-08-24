// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_
#define RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_

#include <utility>

#include <base/functional/callback.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/utils/multi_function_runner.h"

namespace runtime_probe {

class GenericCameraFunction : public ProbeFunction {
  using ProbeFunction::ProbeFunction;

 public:
  NAME_PROBE_FUNCTION("generic_camera");

 private:
  // ProbeFunction overrides.
  bool PostParseArguments() override;
  void EvalAsyncImpl(
      base::OnceCallback<void(DataType)> callback) const override {
    runner_.Run(std::move(callback));
  }

  MultiFunctionRunner runner_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_CAMERA_H_
