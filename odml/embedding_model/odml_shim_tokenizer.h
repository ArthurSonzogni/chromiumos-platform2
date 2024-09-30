// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_ODML_SHIM_TOKENIZER_H_
#define ODML_EMBEDDING_MODEL_ODML_SHIM_TOKENIZER_H_

#include <optional>
#include <string>
#include <vector>

#include <base/memory/raw_ref.h>

#include "odml/embedding_model/tokenizer.h"
#include "odml/utils/odml_shim_loader.h"

namespace embedding_model {

class OdmlShimTokenizer : public Tokenizer {
 public:
  explicit OdmlShimTokenizer(const raw_ref<odml::OdmlShimLoader> shim_loader);

  virtual ~OdmlShimTokenizer() = default;

  void Load(base::PassKey<ModelHolder> passkey,
            const std::string& model_path,
            LoadCallback callback) override;

  void Unload(base::PassKey<ModelHolder> passkey) override;

  bool IsLoaded() override;

  std::optional<std::vector<int>> Tokenize(base::PassKey<ModelHolder> passkey,
                                           const std::string& s) override;

 private:
  // Part of Load(), called after shim_loader_ finishes loading.
  void LoadShimReady(const std::string& model_path,
                     LoadCallback callback,
                     bool success);

  // For access to the odml-shim functions, which contains a wrapper to
  // SentencePiece library.
  const raw_ref<odml::OdmlShimLoader> shim_loader_;

  // A pointer to the SentencePiece wrapper returned by odml-shim. We're
  // responsible for free-ing this pointer by calling Unload() in odml-shim if
  // it's not nullptr.
  void* tokenizer_;

  base::WeakPtrFactory<OdmlShimTokenizer> weak_factory_{this};
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_ODML_SHIM_TOKENIZER_H_
