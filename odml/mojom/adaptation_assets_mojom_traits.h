// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_
#define MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_

#include <base/files/file.h>

#include "odml/mojom/file_mojom_traits.h"
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

  static bool Read(on_device_model::mojom::AdaptationAssetsDataView data,
                   on_device_model::AdaptationAssets* assets) {
    return data.ReadWeights(&assets->weights);
  }
};

}  // namespace mojo

#endif  // MOJOM_ADAPTATION_ASSETS_MOJOM_TRAITS_H_
