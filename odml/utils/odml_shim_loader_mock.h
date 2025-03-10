// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_ODML_SHIM_LOADER_MOCK_H_
#define ODML_UTILS_ODML_SHIM_LOADER_MOCK_H_

#include <string>

#include <base/functional/callback.h>
#include <gmock/gmock.h>

#include "odml/utils/odml_shim_loader.h"

namespace odml {

class OdmlShimLoaderMock : public OdmlShimLoader {
 public:
  OdmlShimLoaderMock() = default;
  ~OdmlShimLoaderMock() override = default;

  MOCK_METHOD(bool, IsShimReady, (), (override));
  MOCK_METHOD(void,
              EnsureShimReady,
              (base::OnceCallback<void(bool)> callback),
              (override));
  MOCK_METHOD(void*, GetFunctionPointer, (const std::string& name), (override));
  MOCK_METHOD(void,
              InstallVerifiedShim,
              (base::OnceCallback<void(bool)> callback),
              (override));
};

}  // namespace odml

#endif  // ODML_UTILS_ODML_SHIM_LOADER_MOCK_H_
