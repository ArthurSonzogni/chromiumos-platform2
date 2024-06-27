// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJOM_MODEL_ASSETS_MOJOM_TRAITS_H_
#define MOJOM_MODEL_ASSETS_MOJOM_TRAITS_H_

#include <base/files/file.h>

#include "odml/mojom/file_mojom_traits.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/mojom/read_only_file_mojom_traits.h"
#include "odml/on_device_model/public/cpp/model_assets.h"

namespace mojo {

template <>
struct StructTraits<on_device_model::mojom::ModelAssetsDataView,
                    on_device_model::ModelAssets> {
  static base::File weights(on_device_model::ModelAssets& assets) {
    return std::move(assets.weights);
  }

  static base::File ts_data(on_device_model::ModelAssets& assets) {
    return std::move(assets.ts_data);
  }

  static base::File ts_sp_model(on_device_model::ModelAssets& assets) {
    return std::move(assets.ts_sp_model);
  }

  static base::File language_detection_model(
      on_device_model::ModelAssets& assets) {
    return std::move(assets.language_detection_model);
  }

  static bool Read(on_device_model::mojom::ModelAssetsDataView data,
                   on_device_model::ModelAssets* assets) {
    return data.ReadWeights(&assets->weights) &&
           data.ReadTsData(&assets->ts_data) &&
           data.ReadTsSpModel(&assets->ts_sp_model) &&
           data.ReadLanguageDetectionModel(&assets->language_detection_model);
  }
};

}  // namespace mojo

#endif  // MOJOM_MODEL_ASSETS_MOJOM_TRAITS_H_
