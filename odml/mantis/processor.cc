// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include "odml/mojom/mantis_processor.mojom.h"

namespace mantis {

namespace {
using mojom::MantisError;
using mojom::MantisResult;
}  // namespace

void MantisProcessor::Inpainting(std::vector<uint8> image,
                                 std::vector<unit8> mask,
                                 int seed,
                                 InpaintingCallback callback) {
  auto result = MantisResult::NewError(MantisError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

void MantisProcessor::GenerativeFill(std::vector<uint8> image,
                                     std::vector<unit8> mask,
                                     int seed,
                                     std::string prompt,
                                     GenerativeFillCallback callback) {
  auto result = MantisResult::NewError(MantisError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

}  // namespace mantis
