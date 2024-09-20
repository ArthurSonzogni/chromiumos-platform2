// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/fake/fake_mantis_api.h"

#include <string>
#include <vector>

namespace mantis::fake {
MantisComponent Initialize(std::string assets_path_dir) {
  return {};
}

InpaintingResult Inpainting(ProcessorPtr processor_ptr,
                            const std::vector<uint8_t>& image,
                            const std::vector<uint8_t>& mask,
                            int seed) {
  // Create a fake grayscale image (3x3 pixels)
  std::vector<uint8_t> imageData = {0x00, 0x7F, 0xFF, 0x10, 0x50,
                                    0x90, 0x20, 0x60, 0xA0};
  return {
      .status = MantisStatus::kOk,
      .image = imageData,
  };
}

void DestroyMantisComponent(MantisComponent component) {}

const MantisAPI api = {
    .Initialize = &Initialize,
    .Inpainting = &Inpainting,
    .DestroyMantisComponent = &DestroyMantisComponent,
};

const MantisAPI* GetMantisApi() {
  return &api;
}
}  // namespace mantis::fake
