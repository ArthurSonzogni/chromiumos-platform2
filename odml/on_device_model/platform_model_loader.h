// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_H_
#define ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_H_

#include "base/functional/callback.h"
#include "base/uuid.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace on_device_model {

class PlatformModelLoader {
 public:
  using LoadModelCallback = base::OnceCallback<void(mojom::LoadModelResult)>;

  PlatformModelLoader() = default;
  virtual ~PlatformModelLoader() = default;

  PlatformModelLoader(const PlatformModelLoader&) = delete;
  PlatformModelLoader& operator=(const PlatformModelLoader&) = delete;

  virtual void LoadModelWithUuid(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::OnDeviceModel> model,
      LoadModelCallback callback) = 0;
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_H_
