// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_TOKENIZER_H_
#define ODML_EMBEDDING_MODEL_TOKENIZER_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/types/pass_key.h>

#include "odml/embedding_model/model_holder.h"

namespace embedding_model {

// Tokenizer is an interface to tokenizer such as sentencepiece. This interface
// is designed such that any sequencing or clean-up requirement is handled by
// the caller and not by the class, this is because there may be multiple
// tokenizer implementation but only one ModelHolder so it's best to concentrate
// the complexity of handling these issues in that one class.
class Tokenizer {
 public:
  Tokenizer() = default;

  virtual ~Tokenizer() = default;

  using LoadCallback = base::OnceCallback<void(bool success)>;

  // Load the tokenizer model.
  // The caller is responsible to calling Unload() before destruction after
  // calling Load(), except at shutdown/termination time whereby memory leak is
  // not an issue. The caller is also responsible to ensure that there's no
  // duplicate or concurrent call to Load().
  // passkey is required because all usage of this class is to be descendant of
  // ModelHolder, as ModelHolder is designed to properly handle the
  // serialization of calls.
  virtual void Load(base::PassKey<ModelHolder> passkey,
                    const std::string& model_path,
                    LoadCallback callback) = 0;

  // Unload the tokenizer model.
  // The caller is responsible to ensure there's no duplicate call to Unload().
  // passkey is required because all usage of this class is to be descendant of
  // ModelHolder, as ModelHolder is designed to properly handle the
  // serialization of calls.
  virtual void Unload(base::PassKey<ModelHolder> passkey) = 0;

  // Return true if it's in a loaded state.
  virtual bool IsLoaded() = 0;

  // Tokenize the string |input|.
  // This can only be called after Load() before Unload().
  // passkey is required because all usage of this class is to be descendant of
  // ModelHolder, as ModelHolder is designed to properly handle the
  // serialization of calls.
  virtual std::optional<std::vector<int>> Tokenize(
      base::PassKey<ModelHolder> passkey, const std::string& input) = 0;
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_TOKENIZER_H_
