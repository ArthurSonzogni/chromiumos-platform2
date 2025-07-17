// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_
#define ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_

#include <string>
#include <variant>
#include <vector>

#include "odml/on_device_model/ml/chrome_ml_audio_buffer.h"

namespace ml {

enum class Token {
  // Prefix for system text.
  kSystem,
  // Prefix for model text.
  kModel,
  // Prefix for user text.
  kUser,
  // End a system/model/user section.
  kEnd,
};

// TODO(b/353900545): Fix the skia code, and sync skia definition with g3.
struct SkBitmap {
  char x[56];
};

using InputPiece =
    std::variant<Token, std::string, SkBitmap, AudioBuffer, bool>;

// Options for specifying the performance characteristics of the model to load.
enum class ModelPerformanceHint {
  kHighestQuality,
  kFastestInference,
};

// Type of the backend to run the model.
enum class ModelBackendType {
  // The default WebGPU backend.
  kGpuBackend,
  // The APU accelerator backend. Only available on devices with APU, and need
  // special APU model files.
  kApuBackend,
  // The CPU backend.
  kCpuBackend,
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_CHROME_ML_TYPES_H_
