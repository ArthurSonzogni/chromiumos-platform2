// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/fake/fake_mantis_api.h"

#include <string>

namespace mantis::fake {
MantisComponent Initialize(std::string assets_path_dir) {
  return {};
}

void DestroyMantisComponent(MantisComponent component) {}

const MantisAPI api = {
    .Initialize = &Initialize,
    .DestroyMantisComponent = &DestroyMantisComponent,
};

const MantisAPI* GetMantisApi() {
  return &api;
}
}  // namespace mantis::fake
