// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_ODML_SHIM_LOADER_IMPL_H_
#define ODML_UTILS_ODML_SHIM_LOADER_IMPL_H_

#include "odml/utils/odml_shim_loader.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/native_library.h>
#include <base/scoped_native_library.h>
#include <base/types/expected.h>

namespace odml {

class OdmlShimLoaderImpl : public OdmlShimLoader {
 public:
  OdmlShimLoaderImpl();

  // OdmlShimLoader functions.
  bool IsShimReady() override;
  void EnsureShimReady(base::OnceCallback<void(bool)> callback) override;
  void* GetFunctionPointer(const std::string& name) override;

 private:
  void OnInstallDlcComplete(base::OnceCallback<void(bool)> callback,
                            base::expected<base::FilePath, std::string> result);

  base::ScopedNativeLibrary library_;

  base::WeakPtrFactory<OdmlShimLoaderImpl> weak_ptr_factory_{this};
};

}  // namespace odml

#endif  // ODML_UTILS_ODML_SHIM_LOADER_IMPL_H_
