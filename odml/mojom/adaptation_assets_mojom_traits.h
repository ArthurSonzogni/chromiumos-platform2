// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_
#define MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_

#include <base/files/file.h>

#include "odml/mojom/file_mojom_traits.h"
#include "odml/mojom/file_path_mojom_traits.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/read_only_file_mojom_traits.h"
#include "odml/on_device_model/public/cpp/model_assets.h"

namespace mojo {

template <>
struct StructTraits<on_device_model::mojom::AdaptationAssetsDataView,
                    on_device_model::AdaptationAssets> {
  static base::File weights(on_device_model::AdaptationAssets& assets) {
    return std::move(assets.weights);
  }

  static base::FilePath weights_path(
      on_device_model::AdaptationAssets& assets) {
    return std::move(assets.weights_path);
  }

  static bool Read(on_device_model::mojom::AdaptationAssetsDataView data,
                   on_device_model::AdaptationAssets* assets) {
    // base::FilePath doesn't have nullable StructTraits, so we need to use
    // optional.
    std::optional<base::FilePath> weights_path;
    bool ok = data.ReadWeights(&assets->weights) &&
              data.ReadWeightsPath(&weights_path);
    if (!ok) {
      return false;
    }
    if (weights_path.has_value()) {
      assets->weights_path = *weights_path;
    }
    return true;
  }
};

}  // namespace mojo

#endif  // MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_
