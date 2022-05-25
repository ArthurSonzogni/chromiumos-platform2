// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_

#include <string>

#include <base/callback.h>
#include <brillo/errors/error.h>

namespace diagnostics {

// Adapter for communication with debugd daemon.
class DebugdAdapter {
 public:
  using OnceStringResultCallback =
      base::OnceCallback<void(const std::string& result, brillo::Error* error)>;

  virtual ~DebugdAdapter() = default;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_H_
