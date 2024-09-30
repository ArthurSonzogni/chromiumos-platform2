// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/odml_shim_tokenizer.h"

#include <optional>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace embedding_model {

namespace {
using LoadTokenizerFunction = void* (*)(const char*, size_t);
using UnloadTokenizerFunction = void (*)(void*);
using TokenizeFunction =
    std::optional<std::vector<int>> (*)(void*, const std::string&);
}  // namespace

OdmlShimTokenizer::OdmlShimTokenizer(
    const raw_ref<odml::OdmlShimLoader> shim_loader)
    : shim_loader_(shim_loader), tokenizer_(nullptr) {}

void OdmlShimTokenizer::Load(base::PassKey<ModelHolder> passkey,
                             const std::string& model_path,
                             LoadCallback callback) {
  CHECK(!tokenizer_);
  if (shim_loader_->IsShimReady()) {
    LoadShimReady(model_path, std::move(callback), true);
    return;
  }

  shim_loader_->EnsureShimReady(base::BindOnce(
      &OdmlShimTokenizer::LoadShimReady, weak_factory_.GetWeakPtr(), model_path,
      std::move(callback)));
}

void OdmlShimTokenizer::LoadShimReady(const std::string& model_path,
                                      LoadCallback callback,
                                      bool success) {
  if (!success) {
    LOG(ERROR) << "Shim loader is not ready, cannot load tokenizer.";
    std::move(callback).Run(false);
    return;
  }
  CHECK(shim_loader_->IsShimReady());

  const base::FilePath path(model_path);
  std::string spm_data;
  if (!base::ReadFileToString(path, &spm_data)) {
    LOG(ERROR) << "Unable to read spm model: " << path;
    std::move(callback).Run(false);
    return;
  }

  auto load_tokenizer_fn =
      shim_loader_->Get<LoadTokenizerFunction>("LoadTokenizer");
  if (!load_tokenizer_fn) {
    // This only happens if there's a mismatch between the odml-shim version and
    // odmld's version, whereby we need tokenizer but the odml-shim doesn't have
    // it.
    LOG(ERROR) << "Unable to load spm model because odml-shim doesn't support "
                  "tokenizer.";
    std::move(callback).Run(false);
  }

  tokenizer_ = load_tokenizer_fn(spm_data.data(), spm_data.size());
  if (!tokenizer_) {
    LOG(ERROR) << "Failed to load spm model: " << path;
    std::move(callback).Run(false);
    return;
  }

  std::move(callback).Run(true);
}

void OdmlShimTokenizer::Unload(base::PassKey<ModelHolder> passkey) {
  CHECK(tokenizer_);
  auto unload_tokenizer_fn =
      shim_loader_->Get<UnloadTokenizerFunction>("UnloadTokenizer");
  CHECK(unload_tokenizer_fn)
      << "No UnloadTokenizer() in odml-shim despite LoadTokenizer() existing.";
  unload_tokenizer_fn(tokenizer_);
  tokenizer_ = nullptr;
}

bool OdmlShimTokenizer::IsLoaded() {
  return tokenizer_;
}

std::optional<std::vector<int>> OdmlShimTokenizer::Tokenize(
    base::PassKey<ModelHolder> passkey, const std::string& s) {
  CHECK(tokenizer_);
  auto tokenize_fn = shim_loader_->Get<TokenizeFunction>("Tokenize");
  if (!tokenize_fn) {
    LOG(ERROR)
        << "No Tokenize() in odml-shim despite LoadTokenizer() existing.";
    return std::nullopt;
  }
  return tokenize_fn(tokenizer_, s);
}

}  // namespace embedding_model
