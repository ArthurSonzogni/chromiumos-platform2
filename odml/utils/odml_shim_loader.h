// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_ODML_SHIM_LOADER_H_
#define ODML_UTILS_ODML_SHIM_LOADER_H_

#include <string>

#include <base/functional/callback.h>

namespace odml {

class OdmlShimLoader {
 public:
  OdmlShimLoader() = default;
  virtual ~OdmlShimLoader() = default;

  // Return true if the shim is ready to use.
  virtual bool IsShimReady() = 0;

  // Ensure the shim is ready.
  // The client should wait for the shim is ready before trying to get
  // the function pointer.
  virtual void EnsureShimReady(base::OnceCallback<void(bool)> callback) = 0;

  // Get the function pointer with the function name.
  template <typename T>
  T Get(const std::string& name) {
    return reinterpret_cast<T>(GetFunctionPointer(name));
  }

 protected:
  // Get the raw function pointer with the function name.
  virtual void* GetFunctionPointer(const std::string& name) = 0;
};

}  // namespace odml

#endif  // ODML_UTILS_ODML_SHIM_LOADER_H_
