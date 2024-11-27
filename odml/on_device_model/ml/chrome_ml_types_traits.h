// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_TRAITS_H_
#define ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_TRAITS_H_

#include <string>

#include "odml/mojom/bitmap.mojom.h"
#include "odml/mojom/on_device_model_service.mojom-shared.h"
#include "odml/on_device_model/ml/chrome_ml_types.h"

namespace mojo {

template <>
struct EnumTraits<on_device_model::mojom::Token, ml::Token> {
  static on_device_model::mojom::Token ToMojom(ml::Token input);
  static bool FromMojom(on_device_model::mojom::Token input, ml::Token* output);
};

template <>
struct UnionTraits<on_device_model::mojom::InputPieceDataView, ml::InputPiece> {
  static on_device_model::mojom::InputPieceDataView::Tag GetTag(
      const ml::InputPiece& input_piece);

  static ml::Token token(const ml::InputPiece& input_piece) {
    return std::get<ml::Token>(input_piece);
  }

  static const std::string& text(const ml::InputPiece& input_piece) {
    return std::get<std::string>(input_piece);
  }

  // TODO(b/353900545): Add skia support for crrev.com/c/6038925
  static skia::mojom::BitmapMappedFromTrustedProcessPtr bitmap(
      const ml::InputPiece& input_piece) {
    return skia::mojom::BitmapMappedFromTrustedProcess::New();
  }

  static bool unknown_type(const ml::InputPiece& input_piece) {
    return std::get<bool>(input_piece);
  }

  static bool Read(on_device_model::mojom::InputPieceDataView in,
                   ml::InputPiece* out);
};

}  // namespace mojo

#endif  // ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_TRAITS_H_
