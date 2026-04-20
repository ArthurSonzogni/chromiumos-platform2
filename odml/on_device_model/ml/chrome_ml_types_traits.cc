// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/chrome_ml_types_traits.h"

#include <algorithm>
#include <string>
#include <utility>

#include <base/notreached.h>

namespace mojo {

// static
on_device_model::mojom::Token
EnumTraits<on_device_model::mojom::Token, ml::Token>::ToMojom(ml::Token input) {
  switch (input) {
    case ml::Token::kSystem:
      return on_device_model::mojom::Token::kSystem;
    case ml::Token::kModel:
      return on_device_model::mojom::Token::kModel;
    case ml::Token::kUser:
      return on_device_model::mojom::Token::kUser;
    case ml::Token::kEnd:
      return on_device_model::mojom::Token::kEnd;
  }
  NOTREACHED();
}

// static
ml::Token EnumTraits<on_device_model::mojom::Token, ml::Token>::FromMojom(
    on_device_model::mojom::Token input) {
  switch (input) {
    case on_device_model::mojom::Token::kSystem:
      return ml::Token::kSystem;
    case on_device_model::mojom::Token::kModel:
      return ml::Token::kModel;
    case on_device_model::mojom::Token::kUser:
      return ml::Token::kUser;
    case on_device_model::mojom::Token::kEnd:
      return ml::Token::kEnd;
  }
  NOTREACHED();
}

// static
on_device_model::mojom::InputPieceDataView::Tag
UnionTraits<on_device_model::mojom::InputPieceDataView, ml::InputPiece>::GetTag(
    const ml::InputPiece& input_piece) {
  if (std::holds_alternative<ml::Token>(input_piece)) {
    return on_device_model::mojom::InputPieceDataView::Tag::kToken;
  } else if (std::holds_alternative<std::string>(input_piece)) {
    return on_device_model::mojom::InputPieceDataView::Tag::kText;
  } else if (std::holds_alternative<ml::AudioBuffer>(input_piece)) {
    return on_device_model::mojom::InputPieceDataView::Tag::kAudio;
  } else {
    // TODO(b/353900545): Add skia support for crrev.com/c/6038925
    return on_device_model::mojom::InputPieceDataView::Tag::kUnknownType;
  }
  NOTREACHED();
}

// static
bool UnionTraits<on_device_model::mojom::InputPieceDataView, ml::InputPiece>::
    Read(on_device_model::mojom::InputPieceDataView in, ml::InputPiece* out) {
  switch (in.tag()) {
    case on_device_model::mojom::InputPieceDataView::Tag::kToken: {
      ml::Token token;
      if (!in.ReadToken(&token)) {
        return false;
      }
      *out = token;
      return true;
    }
    case on_device_model::mojom::InputPieceDataView::Tag::kText: {
      std::string text;
      if (!in.ReadText(&text)) {
        return false;
      }
      *out = std::move(text);
      return true;
    }
    case on_device_model::mojom::InputPieceDataView::Tag::kAudio: {
      ml::AudioBuffer audio;
      if (!in.ReadAudio(&audio)) {
        return false;
      }
      *out = std::move(audio);
      return true;
    }
    // TODO(b/353900545): Add skia support for crrev.com/c/6038925
    case on_device_model::mojom::InputPieceDataView::Tag::kBitmap:
    case on_device_model::mojom::InputPieceDataView::Tag::kUnknownType: {
      *out = in.unknown_type();
      return true;
    }
  }
  return false;
}

// static
on_device_model::mojom::ModelBackendType
EnumTraits<on_device_model::mojom::ModelBackendType,
           ml::ModelBackendType>::ToMojom(ml::ModelBackendType input) {
  switch (input) {
    case ml::ModelBackendType::kGpuBackend:
      return on_device_model::mojom::ModelBackendType::kGpu;
    case ml::ModelBackendType::kApuBackend:
      return on_device_model::mojom::ModelBackendType::kApu;
    case ml::ModelBackendType::kCpuBackend:
      return on_device_model::mojom::ModelBackendType::kCpu;
  }
  NOTREACHED();
}

// static
ml::ModelBackendType
EnumTraits<on_device_model::mojom::ModelBackendType, ml::ModelBackendType>::
    FromMojom(on_device_model::mojom::ModelBackendType input) {
  switch (input) {
    case on_device_model::mojom::ModelBackendType::kGpu:
      return ml::ModelBackendType::kGpuBackend;
    case on_device_model::mojom::ModelBackendType::kApu:
      return ml::ModelBackendType::kApuBackend;
    case on_device_model::mojom::ModelBackendType::kCpu:
      return ml::ModelBackendType::kCpuBackend;
  }
  NOTREACHED();
}

// static
on_device_model::mojom::ModelPerformanceHint
EnumTraits<on_device_model::mojom::ModelPerformanceHint,
           ml::ModelPerformanceHint>::ToMojom(ml::ModelPerformanceHint input) {
  switch (input) {
    case ml::ModelPerformanceHint::kHighestQuality:
      return on_device_model::mojom::ModelPerformanceHint::kHighestQuality;
    case ml::ModelPerformanceHint::kFastestInference:
      return on_device_model::mojom::ModelPerformanceHint::kFastestInference;
  }
  NOTREACHED();
}

// static
ml::ModelPerformanceHint
EnumTraits<on_device_model::mojom::ModelPerformanceHint,
           ml::ModelPerformanceHint>::
    FromMojom(on_device_model::mojom::ModelPerformanceHint input) {
  switch (input) {
    case on_device_model::mojom::ModelPerformanceHint::kHighestQuality:
      return ml::ModelPerformanceHint::kHighestQuality;
    case on_device_model::mojom::ModelPerformanceHint::kFastestInference:
      return ml::ModelPerformanceHint::kFastestInference;
  }
  NOTREACHED();
}

bool StructTraits<on_device_model::mojom::AudioDataDataView, ml::AudioBuffer>::
    Read(on_device_model::mojom::AudioDataDataView in, ml::AudioBuffer* out) {
  out->sample_rate_hz = in.sample_rate();
  out->num_channels = in.channel_count();
  out->num_frames = in.frame_count();
  mojo::ArrayDataView<float> data_view;
  in.GetDataDataView(&data_view);
  out->data.reserve(data_view.size());
  std::copy_n(data_view.data(), data_view.size(),
              std::back_inserter(out->data));
  return true;
}

}  // namespace mojo
