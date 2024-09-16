// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_MODEL_INFO_H_
#define ODML_EMBEDDING_MODEL_MODEL_INFO_H_

#include <string>
#include <utility>

namespace embedding_model {

// Contains information specific to model with model_type = "embedding_tflite".
struct EmbeddingTfliteModelInfo {
  // Path to the tflite file.
  std::string tflite_path;

  // Is the tokenizer (sentencepiece) built into the tflite file?
  // Currently we only support false for this.
  bool builtin_spm;
};

// Contain information required to load and run the model.
struct ModelInfo {
  // Indicates what type of model and how to run it.
  // Currently we only support "embedding_tflite", this is a tflite file that
  // can be run through delegate and contains an embedding model.
  std::string model_type;

  // The name and version of the model.
  // If model_version is the same, then it is guaranteed that the embedding
  // result is compatible.
  std::string model_version;

  // type_specific_info holds the information that is specific to the
  // model_type.
  std::variant<struct EmbeddingTfliteModelInfo> type_specific_info;
};

constexpr char kEmbeddingTfliteModelType[] = "embedding_tflite";

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_MODEL_INFO_H_
