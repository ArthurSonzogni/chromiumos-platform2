// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_MODEL_METADATA_H_
#define ML_MODEL_METADATA_H_

#include <map>
#include <string>

#include "mojom/model.mojom.h"

namespace ml {

// The information about one supported model.
struct ModelMetadata {
  chromeos::machine_learning::mojom::ModelId id;
  std::string model_file;

  // As accepted by the constructor of ModelImpl.
  std::map<std::string, int> required_inputs;
  std::map<std::string, int> required_outputs;
};

// Returns a map from model ID to model metdata for each supported model.
std::map<chromeos::machine_learning::mojom::ModelId, ModelMetadata>
GetModelMetadata();

}  // namespace ml

#endif  // ML_MODEL_METADATA_H_
