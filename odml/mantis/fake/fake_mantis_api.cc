// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/fake/fake_mantis_api.h"

#include <string>
#include <vector>

namespace mantis::fake {
namespace {

std::vector<uint8_t> GetFakeImageData() {
  // Create a fake grayscale image (3x3 pixels)
  return {0x00, 0x7F, 0xFF, 0x10, 0x50, 0x90, 0x20, 0x60, 0xA0};
}

MantisComponent Initialize(const std::string& assets_path_dir) {
  return {};
}

InpaintingResult Inpainting(ProcessorPtr processor_ptr,
                            const std::vector<uint8_t>& image,
                            const std::vector<uint8_t>& mask,
                            int seed) {
  return {
      .status = MantisStatus::kOk,
      .image = GetFakeImageData(),
  };
}

GenerativeFillResult GenerativeFill(ProcessorPtr processor_ptr,
                                    const std::vector<uint8_t>& image,
                                    const std::vector<uint8_t>& mask,
                                    int seed,
                                    const std::string& prompt) {
  return {
      .status = MantisStatus::kOk,
      .image = GetFakeImageData(),
  };
}

void DestroyMantisComponent(MantisComponent component) {}

const MantisAPI api = {
    .Initialize = &Initialize,
    .Inpainting = &Inpainting,
    .GenerativeFill = &GenerativeFill,
    .DestroyMantisComponent = &DestroyMantisComponent,
};

}  // namespace

const MantisAPI* GetMantisApi() {
  return &api;
}
}  // namespace mantis::fake
